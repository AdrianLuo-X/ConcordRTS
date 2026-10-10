#include "network/internet/RuntimeInternetCoordinator.h"
#include "network/internet/RuntimeInternetLobbyFlow.h"
#include "network/maps/RuntimeMultiplayerCompatibilityFingerprint.h"
#include "network/lobby/RuntimeRoomPassword.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr const char* serviceId = "memory.internet.service";
constexpr const char* contentFingerprint = "5555666677778888";

struct SmokeContext {
    int failures = 0;

    void expect(bool condition, const std::string& message) {
        if (condition) return;
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
};

struct RuntimeProbeWorld {
    int tick = 0;
    std::vector<RuntimeMultiplayerLockstepApplyCommand> applied;
    std::vector<std::uint8_t> stagedSnapshot;
};

RuntimeMultiplayerRuntimeCallbacks runtimeCallbacksFor(
    RuntimeProbeWorld& world
) {
    RuntimeMultiplayerRuntimeCallbacks callbacks;
    callbacks.prevalidateCommands = [](
        std::span<const RuntimeMultiplayerLockstepApplyCommand>,
        std::string& errorMessage
    ) {
        errorMessage.clear();
        return true;
    };
    callbacks.applyCommands = [&world](
        std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,
        std::string& errorMessage
    ) {
        world.applied.insert(world.applied.end(), batch.begin(), batch.end());
        errorMessage.clear();
        return true;
    };
    callbacks.currentCompletedTick = [&world]() { return world.tick; };
    callbacks.advanceSimulation = [&world](int completedTick) {
        world.tick = completedTick;
    };
    callbacks.authoritativeStateHash = [&world]() {
        std::uint64_t hash = 1469598103934665603ULL;
        const auto mix = [&hash](std::uint64_t value) {
            for (int shift = 56; shift >= 0; shift -= 8) {
                hash ^= static_cast<std::uint8_t>(value >> shift);
                hash *= 1099511628211ULL;
            }
        };
        mix(static_cast<std::uint64_t>(world.tick));
        mix(static_cast<std::uint64_t>(world.applied.size()));
        for (const RuntimeMultiplayerLockstepApplyCommand& item
             : world.applied) {
            mix(static_cast<std::uint64_t>(item.tick));
            mix(item.frameSequence);
            mix(static_cast<std::uint64_t>(item.commandIndex));
            for (const unsigned char byte : item.peerId) mix(byte);
            mix(static_cast<std::uint64_t>(
                runtimeMultiplayerGameplayCommandType(item.command)
            ));
        }
        return hash == 0U ? 1ULL : hash;
    };
    callbacks.matchTerminal = []() { return false; };
    callbacks.stageVerifiedSnapshot = [&world](
        const RuntimeMultiplayerSnapshotDescriptor& descriptor,
        std::span<const std::uint8_t> bytes,
        std::uint64_t& observedHash,
        std::string& errorMessage
    ) {
        world.stagedSnapshot.assign(bytes.begin(), bytes.end());
        observedHash = descriptor.identity.authoritativeStateHash;
        errorMessage.clear();
        return true;
    };
    callbacks.commitStagedSnapshot = [&world](
        const RuntimeMultiplayerSnapshotDescriptor& descriptor,
        std::uint64_t& observedHash,
        std::string& errorMessage
    ) {
        if (world.stagedSnapshot.empty()) {
            errorMessage = "fixture has no staged snapshot";
            return false;
        }
        observedHash = descriptor.identity.authoritativeStateHash;
        errorMessage.clear();
        return true;
    };
    callbacks.discardStagedSnapshot = [&world](
        const RuntimeMultiplayerSnapshotDescriptor&
    ) {
        world.stagedSnapshot.clear();
    };
    callbacks.onLoadTimelineRebound = [](
        const RuntimeMultiplayerSnapshotDescriptor&,
        const RuntimeMultiplayerSessionState&,
        std::string& errorMessage
    ) {
        errorMessage.clear();
        return true;
    };
    return callbacks;
}

RuntimeMultiplayerGameplayCommand runtimeStopCommand() {
    RuntimeMultiplayerGameplayStopCommand stop;
    stop.objectIds = {9, 4, 9};
    RuntimeMultiplayerGameplayCommand command;
    command.body = std::move(stop);
    return command;
}

RuntimeMultiplayerGameplayCommand runtimeGuardCommand() {
    RuntimeMultiplayerGameplayGuardCommand guard;
    guard.objectIds = {7, 3};
    RuntimeMultiplayerGameplayCommand command;
    command.body = std::move(guard);
    return command;
}

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
    std::size_t capacity = 4U * 1024U * 1024U;
    std::size_t maximumChunk = 64U * 1024U;
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
    ) : duplex_(std::move(duplex)),
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
        if (closed()) {
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
        return {RuntimeInternetTcpStreamIoStatus::Progress, copied, {}};
    }

    RuntimeInternetTcpStreamIoResult write(
        std::span<const std::uint8_t> source
    ) override {
        if (closed() || peerClosed()) {
            return {RuntimeInternetTcpStreamIoStatus::Closed, 0, {}};
        }
        MemoryFlow& outgoing = sideA_ ? duplex_->aToB : duplex_->bToA;
        const std::size_t available = outgoing.available();
        if (available >= duplex_->capacity) {
            return {RuntimeInternetTcpStreamIoStatus::WouldBlock, 0, {}};
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
        return {RuntimeInternetTcpStreamIoStatus::Progress, copied, {}};
    }

    void close() noexcept override {
        if (!duplex_) return;
        if (sideA_) duplex_->aClosed = true;
        else duplex_->bClosed = true;
    }

private:
    bool closed() const {
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
    const RuntimeInternetTcpRouteDescriptor& route
) {
    RuntimeInternetAuthenticatedTcpStreamIdentity identity;
    identity.authenticatedServiceId = route.serviceId;
    identity.authenticatedPeerId = route.localPeerId;
    identity.route = route;
    auto duplex = std::make_shared<MemoryDuplex>();
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

class MemoryInternetService;

class MemoryInternetServiceAdapter final
    : public RuntimeInternetServiceAdapter {
public:
    MemoryInternetServiceAdapter(
        std::shared_ptr<MemoryInternetService> service,
        std::string peerId
    ) : service_(std::move(service)), peerId_(std::move(peerId)) {}

    std::string_view authenticatedServiceId() const noexcept override {
        return serviceId;
    }

    std::string_view authenticatedPeerId() const noexcept override {
        return peerId_;
    }

    bool sendCoordinator(
        const RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) override;
    bool sendCoordinatorWithAdmissionProof(
        const RuntimeNetworkPacket& packet,
        std::span<const std::uint8_t> roomAdmissionProof,
        std::string& errorMessage
    ) override;
    bool openAuthenticatedTcpRelay(
        const RuntimeInternetServiceTcpOpenRequest& request,
        std::string& errorMessage
    ) override;
    std::vector<RuntimeInternetServiceEvent> poll(
        int timeoutMilliseconds
    ) override;
    void close() noexcept override { closed_ = true; }

private:
    std::shared_ptr<MemoryInternetService> service_;
    std::string peerId_;
    bool closed_ = false;
};

class MemoryInternetService final
    : public std::enable_shared_from_this<MemoryInternetService> {
public:
    struct PendingRelay {
        std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> host;
        std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> remote;
        std::string remotePeerId;
        RuntimeInternetRole remoteRole = RuntimeInternetRole::Client;
    };

    static std::shared_ptr<MemoryInternetService> create(
        std::string& errorMessage
    ) {
        auto service = std::shared_ptr<MemoryInternetService>(
            new MemoryInternetService()
        );
        if (!service->open(errorMessage)) return nullptr;
        return service;
    }

    std::unique_ptr<RuntimeInternetServiceAdapter> adapter(
        std::string peerId
    ) {
        return std::make_unique<MemoryInternetServiceAdapter>(
            shared_from_this(),
            std::move(peerId)
        );
    }

    void setNow(std::uint64_t now) { nowMilliseconds_ = now; }

    bool sendCoordinator(
        const std::string& peerId,
        const RuntimeNetworkPacket& packet,
        std::span<const std::uint8_t> roomAdmissionProof,
        std::string& errorMessage
    ) {
        if (packet.type == runtimeInternetCommunityPacketType) {
            if (!roomAdmissionProof.empty()) {
                errorMessage =
                    "community packets must not carry room admission proof";
                return false;
            }
            RuntimeInternetCommunityServiceEnvelope envelope;
            envelope.authenticatedServiceId = serviceId;
            envelope.authenticatedPeerId = peerId;
            envelope.packet = packet;
            std::vector<RuntimeInternetCommunityServiceAction> actions;
            if (!community_.receive(envelope, actions, errorMessage)) {
                return false;
            }
            return applyCommunityActions(
                std::move(actions),
                errorMessage
            );
        }
        if (packet.type == runtimeInternetMatchmakingPacketType) {
            if (!roomAdmissionProof.empty()) {
                errorMessage =
                    "matchmaking packets must not carry room admission proof";
                return false;
            }
            RuntimeInternetMatchmakingServiceEnvelope envelope;
            envelope.authenticatedServiceId = serviceId;
            envelope.authenticatedPeerId = peerId;
            envelope.packet = packet;
            std::vector<RuntimeInternetMatchmakingServiceAction> actions;
            if (!matchmaking_.receive(envelope, actions, errorMessage)) {
                return false;
            }
            return applyMatchmakingActions(
                std::move(actions),
                errorMessage
            );
        }
        RuntimeInternetServiceEnvelope envelope;
        envelope.channel = RuntimeInternetServiceChannel::CoordinatorTls;
        envelope.authenticatedServiceId = serviceId;
        envelope.authenticatedPeerId = peerId;
        envelope.roomAdmissionProof.assign(
            roomAdmissionProof.begin(),
            roomAdmissionProof.end()
        );
        envelope.packet = packet;
        std::vector<RuntimeInternetCoordinatorAction> actions;
        if (!coordinator_.receive(envelope, actions, errorMessage)) {
            return false;
        }
        if (!roomAdmissionProof.empty()) ++protectedCoordinatorSendCount_;
        return applyActions(std::move(actions), errorMessage);
    }

    bool openTcp(
        const std::string& peerId,
        const RuntimeInternetServiceTcpOpenRequest& request,
        std::string& errorMessage
    ) {
        RuntimeInternetControlMessage message;
        if (!decodeRuntimeInternetControlMessage(
                request.relayBindPacket,
                message,
                errorMessage
            )) {
            return false;
        }
        const auto* bind = std::get_if<RuntimeInternetRelayBindBody>(
            &message.body
        );
        if (!bind || message.type != RuntimeInternetControlType::RelayBind
            || bind->transport != RuntimeInternetRelayTransport::Tcp
            || bind->connectionId != request.route.connectionId
            || message.transactionId != request.route.connectionId
            || request.route.localPeerId != peerId
            || request.endpoint != request.route.endpoint) {
            errorMessage = "invalid memory TCP relay request";
            return false;
        }
        PendingRelay& pending = pendingRelays_[request.route.connectionId];
        std::unique_ptr<RuntimeInternetAuthenticatedTcpStream>* slot =
            request.route.localRole == RuntimeInternetRole::Host
            ? &pending.host
            : &pending.remote;
        if (*slot) {
            errorMessage = "duplicate memory TCP relay endpoint";
            return false;
        }
        const std::string& remotePeerId =
            request.route.localRole == RuntimeInternetRole::Host
            ? request.route.remotePeerId
            : request.route.localPeerId;
        const RuntimeInternetRole remoteRole =
            request.route.localRole == RuntimeInternetRole::Host
            ? request.route.remoteRole
            : request.route.localRole;
        if (!pending.remotePeerId.empty()
            && (pending.remotePeerId != remotePeerId
                || pending.remoteRole != remoteRole)) {
            errorMessage = "memory TCP relay peer identity changed between legs";
            return false;
        }
        pending.remotePeerId = remotePeerId;
        pending.remoteRole = remoteRole;
        MemoryStreamPair streams = makeMemoryStreamPair(request.route);
        *slot = std::move(streams.service);

        RuntimeInternetServiceEnvelope envelope;
        envelope.channel = RuntimeInternetServiceChannel::RelayTcp;
        envelope.authenticatedServiceId = serviceId;
        envelope.authenticatedPeerId = peerId;
        envelope.packet = request.relayBindPacket;
        std::vector<RuntimeInternetCoordinatorAction> actions;
        if (!coordinator_.receive(envelope, actions, errorMessage)
            || !applyActions(std::move(actions), errorMessage)) {
            return false;
        }

        RuntimeInternetServiceEvent opened;
        opened.kind = RuntimeInternetServiceEventKind::TcpStreamOpened;
        opened.authenticatedServiceId = serviceId;
        opened.authenticatedPeerId = peerId;
        opened.connectionId = request.route.connectionId;
        opened.tcpStream = std::move(streams.endpoint);
        inboxes_[peerId].push_back(std::move(opened));
        ++tcpOpenCount_;
        errorMessage.clear();
        return true;
    }

    std::vector<RuntimeInternetServiceEvent> pollPeer(
        const std::string& peerId
    ) {
        std::string error;
        if (!pump(error)) {
            RuntimeInternetServiceEvent failed;
            failed.kind = RuntimeInternetServiceEventKind::Failure;
            failed.authenticatedServiceId = serviceId;
            failed.authenticatedPeerId = peerId;
            failed.reason = std::move(error);
            inboxes_[peerId].push_back(std::move(failed));
        }
        std::vector<RuntimeInternetServiceEvent> events;
        auto found = inboxes_.find(peerId);
        if (found == inboxes_.end()) return events;
        events.reserve(found->second.size());
        while (!found->second.empty()) {
            events.push_back(std::move(found->second.front()));
            found->second.pop_front();
        }
        return events;
    }

    bool takeCoordinatorEvent(
        const std::string& peerId,
        RuntimeInternetControlType type,
        std::uint64_t transactionId,
        RuntimeInternetServiceEvent& event
    ) {
        auto found = inboxes_.find(peerId);
        if (found == inboxes_.end()) return false;
        for (auto iterator = found->second.begin();
             iterator != found->second.end();
             ++iterator) {
            if (iterator->kind
                != RuntimeInternetServiceEventKind::CoordinatorPacket) {
                continue;
            }
            RuntimeInternetControlMessage message;
            std::string error;
            if (!decodeRuntimeInternetControlMessage(
                    iterator->packet,
                    message,
                    error
                )
                || message.type != type
                || message.transactionId != transactionId) {
                continue;
            }
            event = std::move(*iterator);
            found->second.erase(iterator);
            return true;
        }
        return false;
    }

    void queueEvent(RuntimeInternetServiceEvent event) {
        inboxes_[event.authenticatedPeerId].push_back(std::move(event));
    }

    std::size_t tcpOpenCount() const { return tcpOpenCount_; }
    std::size_t udpOpenCount() const { return 0; }
    std::size_t protectedCoordinatorSendCount() const {
        return protectedCoordinatorSendCount_;
    }
    std::size_t activeRelayCount() const { return activeRelays_.size(); }
    std::size_t pendingRelayCount() const { return pendingRelays_.size(); }
    RuntimeInternetCommunityServiceStats communityStats() const {
        return community_.stats();
    }

    bool failOnePendingRelayForPeer(
        const std::string& peerId,
        std::string reason
    ) {
        if (pendingRelays_.empty()) return false;
        auto pending = pendingRelays_.begin();
        const std::uint64_t connectionId = pending->first;
        if (pending->second.host) pending->second.host->close();
        if (pending->second.remote) pending->second.remote->close();
        std::vector<RuntimeInternetCoordinatorAction> actions;
        std::string error;
        if (!coordinator_.relayChannelClosed(
                connectionId,
                RuntimeInternetRelayTransport::Tcp,
                serviceId,
                peerId,
                actions,
                error
            )
            || !applyActions(std::move(actions), error)) {
            return false;
        }

        RuntimeInternetServiceEvent failed;
        failed.kind = RuntimeInternetServiceEventKind::Failure;
        failed.authenticatedServiceId = serviceId;
        failed.authenticatedPeerId = peerId;
        failed.connectionId = connectionId;
        failed.reason = std::move(reason);
        inboxes_[peerId].push_back(std::move(failed));
        return true;
    }

    bool disconnectOneActiveRelay() {
        if (activeRelays_.empty()) return false;
        auto relay = activeRelays_.begin();
        activeRelayPeers_.erase(relay->first);
        relay->second->close();
        activeRelays_.erase(relay);
        return true;
    }

    bool disconnectActiveRelayForPeer(const std::string& peerId) {
        const auto identity = std::find_if(
            activeRelayPeers_.begin(),
            activeRelayPeers_.end(),
            [&peerId](const auto& entry) {
                return entry.second.first == peerId;
            }
        );
        if (identity == activeRelayPeers_.end()) return false;
        const auto relay = activeRelays_.find(identity->first);
        if (relay == activeRelays_.end()) return false;
        relay->second->close();
        activeRelays_.erase(relay);
        activeRelayPeers_.erase(identity);
        return true;
    }

private:
    bool open(std::string& errorMessage) {
        RuntimeInternetMatchmakingConfig matchmakingConfig;
        matchmakingConfig.serviceId = serviceId;
        matchmakingConfig.queueLeaseMilliseconds = 60000;
        matchmakingConfig.matchLeaseMilliseconds = 60000;
        matchmakingConfig.maximumQueuedPeers = 16;
        matchmakingConfig.maximumActiveMatches = 8;
        matchmakingConfig.admissionProofBytes = 32;
        matchmakingConfig.baseSkillWindow = 100;
        matchmakingConfig.skillWindowGrowthPerSecond = 25;
        matchmakingConfig.maximumSkillWindow = 1000;
        matchmakingConfig.nowMilliseconds = [this]() {
            return nowMilliseconds_;
        };
        matchmakingConfig.secureRandomBytes = [state = std::uint64_t{
            0x243f6a8885a308d3ULL
        }](std::uint8_t* destination, std::size_t size) mutable {
            for (std::size_t index = 0; index < size; ++index) {
                state ^= state >> 12U;
                state ^= state << 25U;
                state ^= state >> 27U;
                const std::uint64_t value = state
                    * 0x2545f4914f6cdd1dULL;
                destination[index] = static_cast<std::uint8_t>(value >> 56U);
            }
            return true;
        };
        if (!matchmaking_.open(
                std::move(matchmakingConfig),
                errorMessage
            )) {
            return false;
        }

        RuntimeInternetCommunityServiceConfig communityConfig;
        communityConfig.serviceId = serviceId;
        communityConfig.nowMilliseconds = [this]() {
            return nowMilliseconds_;
        };
        if (!community_.open(
                std::move(communityConfig),
                errorMessage
            )) {
            return false;
        }

        RuntimeInternetCoordinatorConfig config;
        config.serviceId = serviceId;
        config.udpRelayEndpoint = {"memory-relay.invalid", 46000};
        config.tcpRelayEndpoint = {"memory-relay.invalid", 47000};
        config.roomLeaseMilliseconds = 60000;
        config.connectionLeaseMilliseconds = 60000;
        config.relayLeaseMilliseconds = 60000;
        config.replayRetentionMilliseconds = 60000;
        config.maximumRooms = 8;
        config.maximumRoomsPerPeer = 2;
        config.maximumConnections = 32;
        config.maximumConnectionsPerRoom = 16;
        config.maximumSpectatorsPerRoom = 8;
        config.maximumConnectionsPerPeer = 16;
        config.maximumRelayRoutes = 32;
        config.maximumReplayTombstones = 64;
        config.nowMilliseconds = [this]() { return nowMilliseconds_; };
        config.secureRandomBytes = [state = std::uint64_t{
            0x9e3779b97f4a7c15ULL
        }](std::uint8_t* destination, std::size_t size) mutable {
            for (std::size_t index = 0; index < size; ++index) {
                state ^= state >> 12U;
                state ^= state << 25U;
                state ^= state >> 27U;
                const std::uint64_t value = state
                    * 0x2545f4914f6cdd1dULL;
                destination[index] = static_cast<std::uint8_t>(value >> 56U);
            }
            return true;
        };
        config.authorizePasswordJoin = [this](
            const std::string& roomId,
            const std::string& peerId,
            const std::vector<std::uint8_t>& proof
        ) {
            return matchmaking_.authorizeAdmission(
                roomId,
                peerId,
                proof
            );
        };
        return coordinator_.open(std::move(config), errorMessage);
    }

    bool applyMatchmakingActions(
        std::vector<RuntimeInternetMatchmakingServiceAction> actions,
        std::string& errorMessage
    ) {
        for (RuntimeInternetMatchmakingServiceAction& action : actions) {
            RuntimeInternetMatchmakingMessage message;
            if (!decodeRuntimeInternetMatchmakingMessage(
                    action.packet,
                    message,
                    errorMessage
                )) {
                return false;
            }
            if (message.type
                == RuntimeInternetMatchmakingMessageType::Assignment) {
                const auto* assignment = std::get_if<
                    RuntimeInternetMatchmakingAssignment
                >(&message.body);
                if (assignment == nullptr
                    || !community_.authorizeQuickMatch(
                        assignment->matchId,
                        assignment->peerIds,
                        errorMessage,
                        assignment->roomId
                    )) {
                    return false;
                }
            }
            RuntimeInternetServiceEvent event;
            event.kind = RuntimeInternetServiceEventKind::CoordinatorPacket;
            event.authenticatedServiceId = serviceId;
            event.authenticatedPeerId = action.targetPeerId;
            event.packet = std::move(action.packet);
            inboxes_[action.targetPeerId].push_back(std::move(event));
        }
        errorMessage.clear();
        return true;
    }

    bool applyCommunityActions(
        std::vector<RuntimeInternetCommunityServiceAction> actions,
        std::string& errorMessage
    ) {
        for (RuntimeInternetCommunityServiceAction& action : actions) {
            RuntimeInternetServiceEvent event;
            event.kind = RuntimeInternetServiceEventKind::CoordinatorPacket;
            event.authenticatedServiceId = serviceId;
            event.authenticatedPeerId = action.targetPeerId;
            event.packet = std::move(action.packet);
            inboxes_[action.targetPeerId].push_back(std::move(event));
        }
        errorMessage.clear();
        return true;
    }

    bool applyActions(
        std::vector<RuntimeInternetCoordinatorAction> actions,
        std::string& errorMessage
    ) {
        for (RuntimeInternetCoordinatorAction& action : actions) {
            if (action.kind == RuntimeInternetCoordinatorActionKind::SendCoordinator) {
                RuntimeInternetControlMessage control;
                if (!decodeRuntimeInternetControlMessage(action.packet, control, errorMessage)) return false;
                if (control.type == RuntimeInternetControlType::RoomSealed) {
                    std::vector<RuntimeInternetCommunityServiceAction> rankedActions;
                    if (!community_.rankedRoomStarted(control.roomId, rankedActions, errorMessage)
                        || !applyCommunityActions(std::move(rankedActions), errorMessage)) return false;
                }
            }
            if (action.kind
                == RuntimeInternetCoordinatorActionKind::SendCoordinator
                || action.kind
                    == RuntimeInternetCoordinatorActionKind::SendRelay) {
                RuntimeInternetServiceEvent event;
                event.kind = action.kind
                        == RuntimeInternetCoordinatorActionKind::SendCoordinator
                    ? RuntimeInternetServiceEventKind::CoordinatorPacket
                    : RuntimeInternetServiceEventKind::RelayPacket;
                event.authenticatedServiceId = serviceId;
                event.authenticatedPeerId = action.targetPeerId;
                event.source = action.relayEndpoint;
                event.packet = std::move(action.packet);
                event.connectionId = action.connectionId;
                inboxes_[action.targetPeerId].push_back(std::move(event));
                continue;
            }
            if (action.kind
                == RuntimeInternetCoordinatorActionKind::DeactivateRelayRoute) {
                activeRelays_.erase(action.connectionId);
                activeRelayPeers_.erase(action.connectionId);
                pendingRelays_.erase(action.connectionId);
                continue;
            }
            auto pending = pendingRelays_.find(action.connectionId);
            if (pending == pendingRelays_.end() || !pending->second.host
                || !pending->second.remote) {
                errorMessage = "relay activation preceded both TCP streams";
                return false;
            }
            auto relay = std::make_unique<RuntimeInternetTcpRelayPair>();
            RuntimeInternetTcpTunnelProxyConfig config;
            config.idleTimeoutMilliseconds = 60000;
            if (!relay->open(
                    config,
                    std::move(pending->second.host),
                    std::move(pending->second.remote),
                    nowMilliseconds_,
                    errorMessage
                )) {
                return false;
            }
            activeRelayPeers_[action.connectionId] = {
                pending->second.remotePeerId,
                pending->second.remoteRole
            };
            pendingRelays_.erase(pending);
            activeRelays_[action.connectionId] = std::move(relay);
        }
        errorMessage.clear();
        return true;
    }

    bool pump(std::string& errorMessage) {
        std::vector<RuntimeInternetMatchmakingServiceAction> matchmakingActions
            = matchmaking_.poll(errorMessage);
        if (!errorMessage.empty()
            || !applyMatchmakingActions(
                std::move(matchmakingActions),
                errorMessage
            )) {
            return false;
        }
        if (!applyActions(coordinator_.poll(), errorMessage)) return false;
        std::vector<RuntimeInternetCommunityServiceAction> rankedActions;
        if (!community_.poll(rankedActions, errorMessage) || !applyCommunityActions(std::move(rankedActions), errorMessage)) return false;
        for (auto iterator = activeRelays_.begin();
             iterator != activeRelays_.end();) {
            const RuntimeInternetTcpRelayPairPollResult result =
                iterator->second->poll(nowMilliseconds_);
            if (!result.succeeded && !result.closed) {
                errorMessage = result.error;
                return false;
            }
            if (result.closed) {
                activeRelayPeers_.erase(iterator->first);
                iterator = activeRelays_.erase(iterator);
            } else {
                ++iterator;
            }
        }
        return true;
    }

    RuntimeInternetMatchmakingService matchmaking_;
    RuntimeInternetCommunityService community_;
    RuntimeInternetCoordinator coordinator_;
    std::uint64_t nowMilliseconds_ = 1;
    std::map<std::string, std::deque<RuntimeInternetServiceEvent>> inboxes_;
    std::map<std::uint64_t, PendingRelay> pendingRelays_;
    std::map<
        std::uint64_t,
        std::unique_ptr<RuntimeInternetTcpRelayPair>
    > activeRelays_;
    std::map<
        std::uint64_t,
        std::pair<std::string, RuntimeInternetRole>
    > activeRelayPeers_;
    std::size_t tcpOpenCount_ = 0;
    std::size_t protectedCoordinatorSendCount_ = 0;
};

bool MemoryInternetServiceAdapter::sendCoordinator(
    const RuntimeNetworkPacket& packet,
    std::string& errorMessage
) {
    if (closed_) {
        errorMessage = "memory Internet adapter is closed";
        return false;
    }
    return service_->sendCoordinator(peerId_, packet, {}, errorMessage);
}

bool MemoryInternetServiceAdapter::sendCoordinatorWithAdmissionProof(
    const RuntimeNetworkPacket& packet,
    std::span<const std::uint8_t> roomAdmissionProof,
    std::string& errorMessage
) {
    if (closed_) {
        errorMessage = "memory Internet adapter is closed";
        return false;
    }
    return service_->sendCoordinator(
        peerId_,
        packet,
        roomAdmissionProof,
        errorMessage
    );
}

bool MemoryInternetServiceAdapter::openAuthenticatedTcpRelay(
    const RuntimeInternetServiceTcpOpenRequest& request,
    std::string& errorMessage
) {
    if (closed_) {
        errorMessage = "memory Internet adapter is closed";
        return false;
    }
    return service_->openTcp(peerId_, request, errorMessage);
}

std::vector<RuntimeInternetServiceEvent>
MemoryInternetServiceAdapter::poll(int) {
    return closed_
        ? std::vector<RuntimeInternetServiceEvent>{}
        : service_->pollPeer(peerId_);
}

RuntimeMultiplayerCompatibility compatibility() {
    RuntimeMultiplayerCompatibility value;
    value.protocolFingerprint = 0x1111222233334444ULL;
    value.contentFingerprint = 0x5555666677778888ULL;
    return value;
}

RuntimeInternetCandidate candidate(
    const std::string& address,
    std::uint16_t port
) {
    RuntimeInternetCandidate value;
    value.kind = RuntimeInternetCandidateKind::ServerReflexive;
    value.endpoint = {address, port};
    value.priority = 100;
    return value;
}

RuntimeInternetConnectionConfig connectionConfig(
    const std::string& peerId,
    RuntimeInternetRole role,
    std::uint64_t transactionId,
    const std::string& roomId,
    std::uint16_t candidatePort
) {
    RuntimeInternetConnectionConfig config;
    config.serviceId = serviceId;
    config.localPeerId = peerId;
    config.roomId = roomId;
    config.role = role;
    config.contentFingerprint = contentFingerprint;
    config.localCandidates = {
        candidate("192.0.2." + std::to_string(candidatePort % 200U + 1U),
            candidatePort)
    };
    config.transactionId = transactionId;
    config.retryMilliseconds = 1;
    config.coordinatorTimeoutMilliseconds = 2000;
    config.punchTimeoutMilliseconds = 2;
    config.relayTimeoutMilliseconds = 3000;
    config.maximumRetries = 32;
    return config;
}

RuntimeInternetConnectionConfig queryConfig(
    const std::string& peerId,
    std::uint64_t transactionId
) {
    RuntimeInternetConnectionConfig config = connectionConfig(
        peerId,
        RuntimeInternetRole::Client,
        transactionId,
        {},
        41000
    );
    config.localCandidates.clear();
    return config;
}

RuntimeInternetRoomRecord roomRecord(
    const std::string& roomId,
    const std::string& hostPeerId,
    const std::string& roomName
) {
    RuntimeInternetRoomRecord room;
    room.roomId = roomId;
    room.roomName = roomName;
    room.hostPeerId = hostPeerId;
    room.contentFingerprint = contentFingerprint;
    room.playerCount = 1;
    room.maximumPlayers = 2;
    return room;
}

RuntimeLanRoomAdvertisement canonicalAdvertisement(
    const RuntimeInternetRoomRecord& room,
    const std::string& hostName
) {
    RuntimeLanRoomAdvertisement result;
    result.roomId = room.roomId;
    result.roomName = room.roomName;
    result.hostName = hostName;
    result.hostAddress = "authenticated-relay.invalid";
    result.contentFingerprint = room.contentFingerprint;
    result.protocolVersion = runtimeLanProtocolVersion;
    result.sessionPort = 1;
    result.playerCount = room.playerCount;
    result.maximumPlayers = room.maximumPlayers;
    result.passwordProtected = room.passwordProtected;
    return result;
}

std::uint64_t mapFingerprint(
    const std::filesystem::path& path,
    const std::string& canonicalId,
    SmokeContext& context
) {
    RuntimeMultiplayerContentComponent component;
    std::string error;
    context.expect(
        loadRuntimeMultiplayerContentComponentFile(
            path,
            canonicalId,
            component,
            error
        ),
        "Internet lobby fixture map should load: " + error
    );
    std::uint64_t fingerprint = 0;
    context.expect(
        runtimeMultiplayerContentFingerprint(
            {component},
            fingerprint,
            error
        ),
        "Internet lobby fixture map should fingerprint: " + error
    );
    return fingerprint;
}

RuntimeMultiplayerLobbyHostCreateOptions lobbyOptions(
    const std::string& hostPeerId,
    const std::string& hostName,
    std::uint64_t mapContentFingerprint
) {
    RuntimeMultiplayerLobbyHostCreateOptions options;
    options.map.canonicalId = "multimd/internet-lobby-flow.map";
    options.map.contentFingerprint = mapContentFingerprint;
    options.rules.gameMode = "battle";
    options.rules.rulesOverride = "mpbattlemd.ini";
    options.rules.randomSeed = 0x1029384756ULL;
    options.rules.fixedTickHz = 30;
    options.rules.startingCredits = 10000;
    options.rules.startingUnits = 10;
    options.rules.techLevel = 10;
    options.rules.gameSpeed = 2;
    options.hostMember.peerId = hostPeerId;
    options.hostMember.displayName = hostName;
    options.hostMember.role = RuntimeMultiplayerRoomMemberRole::Host;
    options.hostMember.participation =
        RuntimeMultiplayerParticipationRole::Combatant;
    options.slotCount = 3;
    options.computerSlotIndices = {2U};
    return options;
}

RuntimeInternetLobbyHostOptions hostOptions(
    const std::string& hostPeerId,
    const std::string& hostName,
    const RuntimeInternetRoomRecord& room,
    std::uint64_t transactionId,
    std::uint64_t mapContentFingerprint,
    std::uint16_t candidatePort
) {
    RuntimeInternetLobbyHostOptions options;
    options.connection = connectionConfig(
        hostPeerId,
        RuntimeInternetRole::Host,
        transactionId,
        room.roomId,
        candidatePort
    );
    options.room = room;
    options.network.session.roomId = room.roomId;
    options.network.session.hostPeerId = hostPeerId;
    options.network.session.hostDisplayName = hostName;
    options.network.session.hostParticipation =
        RuntimeMultiplayerParticipationRole::Combatant;
    options.network.session.compatibility = compatibility();
    options.network.session.maximumMembers = runtimeMultiplayerMaximumMembers;
    options.network.session.stateHashPeriodTicks = 1;
    options.network.roomName = room.roomName;
    // Internet admission is enforced by authenticated coordinator metadata;
    // the private loopback LAN carrier remains unprotected.
    options.network.passwordProtected = false;
    options.network.roomGeneration = transactionId + 0x778899ULL;
    options.lobby = lobbyOptions(
        hostPeerId,
        hostName,
        mapContentFingerprint
    );
    options.proxy.connectTimeoutMilliseconds = 3000;
    options.proxy.idleTimeoutMilliseconds = 10000;
    return options;
}

RuntimeInternetLobbyJoinOptions joinOptions(
    const std::string& peerId,
    const std::string& displayName,
    RuntimeInternetRole role,
    const RuntimeInternetRoomRecord& room,
    const std::string& hostName,
    std::uint64_t transactionId,
    std::uint16_t candidatePort,
    RuntimeMultiplayerCompatibility joinCompatibility = compatibility()
) {
    RuntimeInternetLobbyJoinOptions options;
    options.connection = connectionConfig(
        peerId,
        role,
        transactionId,
        room.roomId,
        candidatePort
    );
    options.room = room;
    options.canonicalRoom = canonicalAdvertisement(room, hostName);
    options.network.peerId = peerId;
    options.network.displayName = displayName;
    options.network.participation = role == RuntimeInternetRole::Spectator
        ? RuntimeMultiplayerParticipationRole::Spectator
        : RuntimeMultiplayerParticipationRole::Combatant;
    options.network.compatibility = joinCompatibility;
    options.network.connectTimeoutMilliseconds = 3000;
    options.proxy.connectTimeoutMilliseconds = 3000;
    options.proxy.idleTimeoutMilliseconds = 10000;
    return options;
}

RuntimeInternetLobbyMatchmakingOptions matchmakingOptions(
    const std::string& peerId,
    std::uint64_t requestId,
    std::uint64_t activationTransactionId,
    std::uint64_t connectionTransactionBase,
    std::uint64_t mapContentFingerprint,
    std::uint16_t candidatePort,
    std::int32_t skillRating
) {
    const std::string displayName = peerId + " Commander";
    const RuntimeInternetRoomRecord templateRoom = roomRecord(
        "match-template-" + peerId,
        peerId,
        "Quick Match Template"
    );
    RuntimeInternetLobbyMatchmakingOptions options;
    options.matchmakingClient.serviceId = serviceId;
    options.matchmakingClient.localPeerId = peerId;
    options.matchmakingClient.heartbeatMilliseconds = 1000;
    options.request.requestId = requestId;
    options.request.queueId = "quick-two-player";
    options.request.regionId = "test-region";
    options.request.contentFingerprint = contentFingerprint;
    options.request.displayName = displayName;
    options.request.playerCount = 2;
    options.request.skillRating = skillRating;
    options.activationTransactionId = activationTransactionId;
    options.hostTemplate = hostOptions(
        peerId,
        displayName,
        templateRoom,
        connectionTransactionBase + 1U,
        mapContentFingerprint,
        candidatePort
    );
    options.joinTemplate = joinOptions(
        peerId,
        displayName,
        RuntimeInternetRole::Client,
        templateRoom,
        displayName,
        connectionTransactionBase + 2U,
        static_cast<std::uint16_t>(candidatePort + 1U)
    );
    return options;
}

bool hasEvent(
    const RuntimeInternetLobbyFlowPollResult& result,
    RuntimeInternetLobbyFlowEventKind kind
) {
    return std::any_of(
        result.events.begin(),
        result.events.end(),
        [&](const RuntimeInternetLobbyFlowEvent& event) {
            return event.kind == kind;
        }
    );
}

RuntimeInternetServiceEvent duplicateCoordinatorEvent(
    const RuntimeInternetServiceEvent& source
) {
    RuntimeInternetServiceEvent duplicate;
    duplicate.kind = source.kind;
    duplicate.authenticatedServiceId = source.authenticatedServiceId;
    duplicate.authenticatedPeerId = source.authenticatedPeerId;
    duplicate.source = source.source;
    duplicate.packet = source.packet;
    duplicate.connectionId = source.connectionId;
    duplicate.reason = source.reason;
    return duplicate;
}

bool pumpUntil(
    const std::shared_ptr<MemoryInternetService>& service,
    const std::vector<RuntimeInternetLobbyFlow*>& flows,
    const std::function<bool()>& complete,
    SmokeContext& context,
    const std::string& description,
    int timeoutMilliseconds = 8000
) {
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMilliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const std::uint64_t now = nowMilliseconds();
        service->setNow(now);
        for (std::size_t flowIndex = 0U;
             flowIndex < flows.size();
             ++flowIndex) {
            RuntimeInternetLobbyFlow* flow = flows[flowIndex];
            RuntimeInternetLobbyFlowPollResult result = flow->poll(now, 0);
            if (!result.succeeded) {
                std::string eventSummary;
                for (const RuntimeMultiplayerLanEvent& event
                     : result.lobbyEvents) {
                    eventSummary += " lan=" + std::to_string(
                        static_cast<unsigned int>(event.kind)
                    );
                }
                for (const RuntimeInternetLobbyFlowEvent& event
                     : result.events) {
                    eventSummary += " internet=" + std::to_string(
                        static_cast<unsigned int>(event.kind)
                    );
                }
                context.expect(
                    false,
                    description + ": flow "
                        + std::to_string(flowIndex)
                        + " poll failed in mode "
                        + std::to_string(
                            static_cast<unsigned int>(flow->mode())
                        ) + ": " + result.error + eventSummary
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

void testHostClientSpectator(
    SmokeContext& context,
    const std::filesystem::path& mapPath,
    std::uint64_t mapContentFingerprint
) {
    std::string error;
    std::shared_ptr<MemoryInternetService> service =
        MemoryInternetService::create(error);
    context.expect(service != nullptr, "memory Internet service: " + error);
    if (!service) return;

    const RuntimeInternetRoomRecord room = roomRecord(
        "internet-room",
        "host-peer",
        "Internet Lobby"
    );
    RuntimeMultiplayerLockstepConfig lockstepConfig;
    lockstepConfig.inputDelayTicks = 0;
    RuntimeInternetLobbyFlow host(
        service->adapter("host-peer"),
        lockstepConfig
    );
    RuntimeInternetLobbyFlow client(
        service->adapter("client-peer"),
        lockstepConfig
    );
    RuntimeInternetLobbyFlow spectator(
        service->adapter("spectator-peer"),
        lockstepConfig
    );

    const std::uint64_t started = nowMilliseconds();
    service->setNow(started);
    const bool hostStarted = host.startHost(
        hostOptions(
            "host-peer",
            "Host",
            room,
            1001,
            mapContentFingerprint,
            41001
        ),
        started,
        error
    );
    context.expect(
        hostStarted,
        "Internet host should start privately: " + error
    );
    if (!pumpUntil(
            service,
            {&host},
            [&host]() {
                return host.mode() == RuntimeInternetLobbyFlowMode::Lobby;
            },
            context,
            "host directory registration"
        )) {
        return;
    }
    context.expect(
        host.primaryConnectionState()
            == RuntimeInternetConnectionState::WaitingForPeer,
        "host registration connection must remain available for more peers"
    );

    // A remote account may request a route and then deliberately never open
    // its relay leg. The host-side relay authentication timeout is scoped to
    // that pending route and must not terminate the shared room.
    RuntimeInternetConnection abandonedJoin;
    RuntimeInternetLobbyJoinOptions abandonedOptions = joinOptions(
        "abandoned-peer",
        "Abandoned",
        RuntimeInternetRole::Client,
        room,
        "Host",
        9001,
        49001
    );
    error.clear();
    context.expect(
        abandonedJoin.beginJoin(
            abandonedOptions.connection,
            nowMilliseconds(),
            error
        ),
        "abandoned join request should begin: " + error
    );
    std::vector<RuntimeInternetAction> abandonedActions =
        abandonedJoin.poll(nowMilliseconds());
    context.expect(
        abandonedActions.size() == 1U
            && abandonedActions.front().kind
                == RuntimeInternetActionKind::SendCoordinator,
        "abandoned join should emit exactly one coordinator request"
    );
    if (abandonedActions.size() != 1U) return;
    error.clear();
    context.expect(
        service->sendCoordinator(
            "abandoned-peer",
            abandonedActions.front().packet,
            {},
            error
        ),
        "abandoned join should reach the coordinator: " + error
    );
    if (!pumpUntil(
            service,
            {&host},
            [&service]() { return service->pendingRelayCount() == 1U; },
            context,
            "host-side half-open relay"
        )) {
        return;
    }
    context.expect(
        service->failOnePendingRelayForPeer(
            "host-peer",
            "fixture half-open relay authentication timeout"
        ),
        "fixture should fail the pending host relay leg"
    );
    service->setNow(nowMilliseconds());
    const RuntimeInternetLobbyFlowPollResult isolatedFailure = host.poll(
        nowMilliseconds(),
        0
    );
    context.expect(
        isolatedFailure.succeeded
            && host.mode() == RuntimeInternetLobbyFlowMode::Lobby
            && host.routeCount() == 0U,
        "an unadmitted half-open relay must be isolated from the host room: "
            + isolatedFailure.error
    );
    if (!isolatedFailure.succeeded) return;
    const std::size_t relayOpenBaseline = service->tcpOpenCount();

    RuntimeInternetLobbyQueryOptions clientQuery;
    clientQuery.connection = queryConfig("client-peer", 2001);
    RuntimeInternetLobbyQueryOptions spectatorQuery;
    spectatorQuery.connection = queryConfig("spectator-peer", 3001);
    error.clear();
    context.expect(
        client.beginRoomQuery(clientQuery, nowMilliseconds(), error),
        "client room query should begin: " + error
    );
    error.clear();
    context.expect(
        spectator.beginRoomQuery(spectatorQuery, nowMilliseconds(), error),
        "spectator room query should begin: " + error
    );
    if (!pumpUntil(
            service,
            {&host, &client, &spectator},
            [&client, &spectator]() {
                return client.mode()
                        == RuntimeInternetLobbyFlowMode::RoomListReady
                    && spectator.mode()
                        == RuntimeInternetLobbyFlowMode::RoomListReady;
            },
            context,
            "authenticated room browsing"
        )) {
        return;
    }
    context.expect(
        client.rooms().size() == 1U
            && spectator.rooms().size() == 1U
            && client.rooms().front() == room
            && spectator.rooms().front() == room,
        "both browser roles should receive the registered room"
    );

    error.clear();
    context.expect(
        client.join(
            joinOptions(
                "client-peer",
                "Client",
                RuntimeInternetRole::Client,
                room,
                "Host",
                2002,
                42001
            ),
            nowMilliseconds(),
            error
        ),
        "client Internet join should begin: " + error
    );
    error.clear();
    context.expect(
        spectator.join(
            joinOptions(
                "spectator-peer",
                "Spectator",
                RuntimeInternetRole::Spectator,
                room,
                "Host",
                3002,
                43001
            ),
            nowMilliseconds(),
            error
        ),
        "spectator Internet join should begin: " + error
    );
    if (!pumpUntil(
            service,
            {&host, &client, &spectator},
            [&host, &client, &spectator]() {
                return host.gameplaySessionCount() == 2U
                    && client.gameplaySessionCount() == 1U
                    && spectator.gameplaySessionCount() == 1U
                    && host.lobbyFlow()->lobby().initialized()
                    && host.lobbyFlow()->lobby().setup().members.size() == 3U
                    && client.lobbyFlow()->lobby().setup().members.size() == 3U
                    && spectator.lobbyFlow()->lobby().setup().members.size()
                        == 3U;
            },
            context,
            "host/client/spectator authenticated admission"
        )) {
        return;
    }
    context.expect(
        host.routeCount() == 2U && client.routeCount() == 1U
            && spectator.routeCount() == 1U,
        "host must retain one independent route per client and spectator"
    );
    context.expect(
        client.primaryConnectionState()
                == RuntimeInternetConnectionState::GameplaySessionConnected
            && spectator.primaryConnectionState()
                == RuntimeInternetConnectionState::GameplaySessionConnected,
        "accepted client and spectator should complete gameplay admission"
    );
    context.expect(
        service->tcpOpenCount() == relayOpenBaseline + 4U
            && service->udpOpenCount() == 0U,
        "Internet gameplay must use four outbound TCP relay legs and no UDP relay"
    );

    error.clear();
    context.expect(
        host.lobbyFlow()->verifyLocalMap(mapPath, error),
        "host Internet map should verify: " + error
    );
    error.clear();
    context.expect(
        client.lobbyFlow()->verifyLocalMap(mapPath, error),
        "client Internet map should verify: " + error
    );
    error.clear();
    context.expect(
        spectator.lobbyFlow()->verifyLocalMap(mapPath, error),
        "spectator Internet map should verify: " + error
    );
    const RuntimeMultiplayerLanOperationResult hostReady =
        host.lobbyFlow()->setReady(true);
    const RuntimeMultiplayerLanOperationResult clientReady =
        client.lobbyFlow()->setReady(true);
    const RuntimeMultiplayerLanOperationResult spectatorReady =
        spectator.lobbyFlow()->setReady(true);
    context.expect(
        hostReady.succeeded && clientReady.succeeded
            && spectatorReady.succeeded,
        "all Internet lobby roles should accept ready intent"
    );
    if (!pumpUntil(
            service,
            {&host, &client, &spectator},
            [&host]() {
                std::string reason;
                return host.lobbyFlow()->canStart(reason);
            },
            context,
            "Internet ready consensus"
        )) {
        return;
    }
    const RuntimeMultiplayerLanOperationResult startedMatch =
        host.lobbyFlow()->startMatch(1);
    context.expect(
        startedMatch.succeeded,
        "Internet host should start the ready room: "
            + startedMatch.reason
    );
    const std::uint64_t sealingNow = nowMilliseconds();
    service->setNow(sealingNow);
    const RuntimeInternetLobbyFlowPollResult sealingStarted = host.poll(
        sealingNow,
        0
    );
    context.expect(
        sealingStarted.succeeded
            && !sealingStarted.matchStarted
            && !hasEvent(
                sealingStarted,
                RuntimeInternetLobbyFlowEventKind::MatchStarted
            )
            && host.mode() == RuntimeInternetLobbyFlowMode::Lobby,
        "host must remain in the sealing phase after canonical Started: "
            + sealingStarted.error
    );
    error.clear();
    context.expect(
        host.takeRunningBridge(error) == nullptr,
        "running bridge must remain unavailable before RoomSealed"
    );

    RuntimeInternetLobbyFlow sealingBrowser(
        service->adapter("sealing-browser-peer"),
        lockstepConfig
    );
    RuntimeInternetLobbyQueryOptions sealingQuery;
    sealingQuery.connection = queryConfig("sealing-browser-peer", 3901U);
    const std::uint64_t sealingQueryNow = nowMilliseconds();
    error.clear();
    context.expect(
        sealingBrowser.beginRoomQuery(
            sealingQuery,
            sealingQueryNow,
            error
        ),
        "room query during seal acknowledgement should begin: " + error
    );
    if (!pumpUntil(
            service,
            {&sealingBrowser},
            [&sealingBrowser]() {
                return sealingBrowser.mode()
                    == RuntimeInternetLobbyFlowMode::RoomListReady;
            },
            context,
            "room hidden before host Running handoff"
        )) {
        return;
    }
    context.expect(
        sealingBrowser.rooms().empty(),
        "sealed room must disappear before the host publishes Running"
    );
    sealingBrowser.close();

    RuntimeInternetConnection sealingJoin;
    RuntimeInternetConnectionConfig sealingJoinConfig = connectionConfig(
        "sealing-join-peer",
        RuntimeInternetRole::Client,
        3902U,
        room.roomId,
        46501
    );
    const std::uint64_t sealingJoinNow = nowMilliseconds();
    error.clear();
    context.expect(
        sealingJoin.beginJoin(
            sealingJoinConfig,
            sealingJoinNow,
            error
        ),
        "join request during seal acknowledgement should begin: " + error
    );
    const std::vector<RuntimeInternetAction> sealingJoinActions =
        sealingJoin.poll(sealingJoinNow);
    context.expect(
        sealingJoinActions.size() == 1U
            && sealingJoinActions.front().kind
                == RuntimeInternetActionKind::SendCoordinator,
        "join request during seal acknowledgement should emit once"
    );
    if (sealingJoinActions.size() == 1U) {
        error.clear();
        const bool joinAccepted = service->sendCoordinator(
            "sealing-join-peer",
            sealingJoinActions.front().packet,
            {},
            error
        );
        context.expect(
            !joinAccepted && error.find("sealed") != std::string::npos,
            "coordinator should explicitly reject a join to the sealed room: "
                + error
        );
    }
    context.expect(
        sealingJoin.state() == RuntimeInternetConnectionState::JoiningRoom,
        "rejected sealed-room join must not receive a peer route"
    );

    const std::uint64_t sealedAckNow = nowMilliseconds();
    service->setNow(sealedAckNow);
    const RuntimeInternetLobbyFlowPollResult sealedAck = host.poll(
        sealedAckNow,
        0
    );
    context.expect(
        sealedAck.succeeded
            && sealedAck.matchStarted
            && hasEvent(
                sealedAck,
                RuntimeInternetLobbyFlowEventKind::MatchStarted
            )
            && host.mode() == RuntimeInternetLobbyFlowMode::Running,
        "RoomSealed should publish host MatchStarted exactly once: "
            + sealedAck.error
    );
    if (!pumpUntil(
        service,
        {&host, &client, &spectator},
        [&host, &client, &spectator]() {
            return host.mode() == RuntimeInternetLobbyFlowMode::Running
                && client.mode() == RuntimeInternetLobbyFlowMode::Running
                && spectator.mode()
                    == RuntimeInternetLobbyFlowMode::Running;
        },
        context,
        "Internet match start broadcast"
    )) {
        return;
    }
    service->setNow(nowMilliseconds());
    const RuntimeInternetLobbyFlowPollResult noDuplicateStart = host.poll(
        nowMilliseconds(),
        0
    );
    context.expect(
        noDuplicateStart.succeeded
            && !noDuplicateStart.matchStarted
            && !hasEvent(
                noDuplicateStart,
                RuntimeInternetLobbyFlowEventKind::MatchStarted
            ),
        "host MatchStarted must not repeat after RoomSealed"
    );

    RuntimeInternetLobbyFlow lateBrowser(
        service->adapter("late-browser-peer"),
        lockstepConfig
    );
    RuntimeInternetLobbyQueryOptions lateQuery;
    lateQuery.connection = queryConfig("late-browser-peer", 4001);
    error.clear();
    context.expect(
        lateBrowser.beginRoomQuery(lateQuery, nowMilliseconds(), error),
        "post-start room query should begin: " + error
    );
    if (!pumpUntil(
            service,
            {&host, &client, &spectator, &lateBrowser},
            [&lateBrowser]() {
                return lateBrowser.mode()
                    == RuntimeInternetLobbyFlowMode::RoomListReady;
            },
            context,
            "started Internet room seal"
        )) {
        return;
    }
    context.expect(
        lateBrowser.rooms().empty(),
        "a started Internet room must be hidden from later browsers"
    );
    lateBrowser.close();

    error.clear();
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> hostBridge =
        host.takeRunningBridge(error);
    context.expect(
        hostBridge != nullptr,
        "running host should transfer its Internet carrier: " + error
    );
    error.clear();
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> clientBridge =
        client.takeRunningBridge(error);
    context.expect(
        clientBridge != nullptr,
        "running client should transfer its Internet carrier: " + error
    );
    error.clear();
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> spectatorBridge =
        spectator.takeRunningBridge(error);
    context.expect(
        spectatorBridge != nullptr,
        "running spectator should transfer its Internet carrier: " + error
    );
    if (!hostBridge || !clientBridge || !spectatorBridge) return;

    context.expect(
        hostBridge->hasExternalOrderedTransport()
            && clientBridge->hasExternalOrderedTransport()
            && spectatorBridge->hasExternalOrderedTransport(),
        "every Internet runtime bridge must own its authenticated relay carrier"
    );
    context.expect(
        service->activeRelayCount() == 2U,
        "host/client/spectator should retain two authenticated relay pairs"
    );

    // The UI destroys or closes the lobby immediately after returning its
    // launch result. These calls must not touch the transferred adapters,
    // connections, streams, or proxies.
    host.close();
    client.close();
    spectator.close();
    context.expect(
        host.mode() == RuntimeInternetLobbyFlowMode::Closed
            && client.mode() == RuntimeInternetLobbyFlowMode::Closed
            && spectator.mode() == RuntimeInternetLobbyFlowMode::Closed,
        "transferred lobby shells should be safely closable"
    );

    RuntimeProbeWorld hostWorld;
    RuntimeProbeWorld clientWorld;
    RuntimeProbeWorld spectatorWorld;
    error.clear();
    context.expect(
        hostBridge->attachRuntime(runtimeCallbacksFor(hostWorld), error),
        "Internet host world should attach after handoff: " + error
    );
    error.clear();
    context.expect(
        clientBridge->attachRuntime(runtimeCallbacksFor(clientWorld), error),
        "Internet client world should attach after handoff: " + error
    );
    error.clear();
    context.expect(
        spectatorBridge->attachRuntime(
            runtimeCallbacksFor(spectatorWorld),
            error
        ),
        "Internet spectator world should attach after handoff: " + error
    );
    if (!hostBridge->runtimeAttached() || !clientBridge->runtimeAttached()
        || !spectatorBridge->runtimeAttached()) {
        return;
    }
    context.expect(
        hostBridge->mayCaptureGameplay()
            && clientBridge->mayCaptureGameplay()
            && !spectatorBridge->mayCaptureGameplay()
            && spectatorBridge->localPeerIsSpectator(),
        "spectator handoff must retain read-only canonical participation"
    );

    service->setNow(nowMilliseconds());
    const RuntimeMultiplayerRuntimePumpResult initialHost =
        hostBridge->pump(true);
    const RuntimeMultiplayerRuntimePumpResult initialClient =
        clientBridge->pump(false);
    const RuntimeMultiplayerRuntimePumpResult initialSpectator =
        spectatorBridge->pump(false);
    context.expect(
        initialHost.succeeded && initialClient.succeeded
            && initialSpectator.succeeded,
        "all handed-off Internet bridges should publish initial hashes"
    );
    if (!initialHost.succeeded || !initialClient.succeeded
        || !initialSpectator.succeeded) {
        return;
    }

    error.clear();
    context.expect(
        hostBridge->captureGameplayCommand(runtimeStopCommand(), error),
        "Internet host command should capture after handoff: " + error
    );
    error.clear();
    context.expect(
        clientBridge->captureGameplayCommand(runtimeGuardCommand(), error),
        "Internet client command should capture after handoff: " + error
    );
    error.clear();
    context.expect(
        !spectatorBridge->captureGameplayCommand(
            runtimeStopCommand(),
            error
        ) && error.find("spectator") != std::string::npos,
        "Internet spectator must remain unable to author gameplay"
    );

    const auto tickDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < tickDeadline
        && (hostWorld.tick < 3 || clientWorld.tick < 3
            || spectatorWorld.tick < 3)) {
        service->setNow(nowMilliseconds());
        const RuntimeMultiplayerRuntimePumpResult hostPump =
            hostBridge->pump(true);
        const RuntimeMultiplayerRuntimePumpResult clientPump =
            clientBridge->pump(false);
        const RuntimeMultiplayerRuntimePumpResult spectatorPump =
            spectatorBridge->pump(false);
        if (!hostPump.succeeded || !clientPump.succeeded
            || !spectatorPump.succeeded) {
            context.expect(
                false,
                "handed-off Internet runtime pump failed: "
                    + (!hostPump.succeeded
                        ? hostPump.error
                        : (!clientPump.succeeded
                            ? clientPump.error
                            : spectatorPump.error))
            );
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(
        hostWorld.tick >= 3 && clientWorld.tick >= 3
            && spectatorWorld.tick >= 3,
        "all Internet roles should commit several canonical runtime ticks"
    );
    context.expect(
        hostWorld.applied == clientWorld.applied
            && hostWorld.applied == spectatorWorld.applied
            && hostWorld.applied.size() == 2U
            && hostWorld.applied.front().peerId == "client-peer"
            && hostWorld.applied.back().peerId == "host-peer",
        "combatants and spectator should apply one peer-sorted canonical batch"
    );

    context.expect(
        service->disconnectActiveRelayForPeer("spectator-peer"),
        "fixture should deliberately sever the spectator relay"
    );
    const int hostTickBeforeSpectatorLoss = hostWorld.tick;
    const int clientTickBeforeSpectatorLoss = clientWorld.tick;
    const int spectatorContinuationTicks = lockstepConfig.inputDelayTicks + 3;
    const auto spectatorDepartureDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    bool spectatorDeparted = false;
    bool combatantFailedDuringSpectatorDeparture = false;
    std::string spectatorDepartureError;
    while (std::chrono::steady_clock::now() < spectatorDepartureDeadline
        && !spectatorDeparted
        && !combatantFailedDuringSpectatorDeparture) {
        service->setNow(nowMilliseconds());
        const RuntimeMultiplayerRuntimePumpResult hostPump =
            hostBridge->pump(true);
        const RuntimeMultiplayerRuntimePumpResult clientPump =
            clientBridge->pump(false);
        combatantFailedDuringSpectatorDeparture = !hostPump.succeeded
            || !clientPump.succeeded || hostBridge->fatal()
            || clientBridge->fatal();
        if (combatantFailedDuringSpectatorDeparture) {
            spectatorDepartureError = !hostPump.succeeded
                ? hostPump.error
                : !clientPump.succeeded
                    ? clientPump.error
                    : hostBridge->fatal()
                        ? hostBridge->fatalReason()
                        : clientBridge->fatalReason();
        }
        const auto spectatorMissing = [](
            const RuntimeMultiplayerRuntimeBridge& bridge
        ) {
            return std::none_of(
                bridge.lan().session().members().begin(),
                bridge.lan().session().members().end(),
                [](const RuntimeMultiplayerRoomMember& member) {
                    return member.peerId == "spectator-peer";
                }
            );
        };
        spectatorDeparted = !combatantFailedDuringSpectatorDeparture
            && hostBridge->lan().session().members().size() == 2U
            && clientBridge->lan().session().members().size() == 2U
            && spectatorMissing(*hostBridge)
            && spectatorMissing(*clientBridge)
            && hostWorld.tick >= hostTickBeforeSpectatorLoss
                + spectatorContinuationTicks
            && clientWorld.tick >= clientTickBeforeSpectatorLoss
                + spectatorContinuationTicks;
        if (!spectatorDeparted
            && !combatantFailedDuringSpectatorDeparture) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    context.expect(
        spectatorDeparted && !combatantFailedDuringSpectatorDeparture
            && service->activeRelayCount() == 1U,
        "spectator relay loss should remove only the spectator while combatants continue"
            " hostMembers="
            + std::to_string(
                hostBridge->lan().session().members().size()
            )
            + " clientMembers="
            + std::to_string(
                clientBridge->lan().session().members().size()
            )
            + " hostTick=" + std::to_string(hostWorld.tick)
            + " clientTick=" + std::to_string(clientWorld.tick)
            + " activeRelays="
            + std::to_string(service->activeRelayCount())
            + " error=" + spectatorDepartureError
    );

    context.expect(
        service->disconnectActiveRelayForPeer("client-peer"),
        "fixture should deliberately sever the combatant relay"
    );
    const auto failureDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    bool observedFatal = false;
    while (std::chrono::steady_clock::now() < failureDeadline
        && !observedFatal) {
        service->setNow(nowMilliseconds());
        const RuntimeMultiplayerRuntimePumpResult hostPump =
            hostBridge->pump(false);
        const RuntimeMultiplayerRuntimePumpResult clientPump =
            clientBridge->pump(false);
        observedFatal = !hostPump.succeeded || !clientPump.succeeded
            || hostBridge->fatal() || clientBridge->fatal();
        if (!observedFatal) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    context.expect(
        observedFatal && (hostBridge->fatal() || clientBridge->fatal()),
        "a combatant relay break must remain a runtime bridge fatal"
    );

    hostBridge.reset();
    clientBridge.reset();
    spectatorBridge.reset();
    service->setNow(nowMilliseconds());
    (void)service->pollPeer("relay-cleanup-probe");
    context.expect(
        service->activeRelayCount() == 0U,
        "runtime bridge destruction should close every remaining relay pair"
    );
}

void testMatchmakingToRunningLobby(
    SmokeContext& context,
    const std::filesystem::path& mapPath,
    std::uint64_t mapContentFingerprint
) {
    std::string error;
    std::shared_ptr<MemoryInternetService> service =
        MemoryInternetService::create(error);
    context.expect(
        service != nullptr,
        "matchmaking memory service should open: " + error
    );
    if (!service) return;

    RuntimeInternetLobbyFlow first(service->adapter("match-first"));
    RuntimeInternetLobbyFlow second(service->adapter("match-second"));
    const std::uint64_t started = nowMilliseconds();
    service->setNow(started);
    context.expect(
        first.beginMatchmaking(
            matchmakingOptions(
                "match-first",
                9001,
                9002,
                9100,
                mapContentFingerprint,
                46001,
                1000
            ),
            started,
            error
        ),
        "first player should enter matchmaking: " + error
    );
    error.clear();
    context.expect(
        second.beginMatchmaking(
            matchmakingOptions(
                "match-second",
                9011,
                9012,
                9200,
                mapContentFingerprint,
                46101,
                1025
            ),
            started,
            error
        ),
        "second player should enter matchmaking: " + error
    );
    error.clear();
    context.expect(
        first.requestCommunityRegistration(
            9003U,
            "RankedFirst",
            error
        ) && second.requestCommunityRegistration(
            9013U,
            "RankedSecond",
            error
        ),
        "Quick Match players should register ranked profiles: " + error
    );

    service->setNow(started + 1U);
    const RuntimeInternetLobbyFlowPollResult secondQueued = second.poll(
        started + 1U,
        0
    );
    context.expect(
        secondQueued.succeeded
            && second.mode() == RuntimeInternetLobbyFlowMode::Matchmaking
            && second.matchAssignment() == nullptr,
        "non-host assignment must remain staged until the host registers"
    );

    service->setNow(started + 2U);
    const RuntimeInternetLobbyFlowPollResult firstAssigned = first.poll(
        started + 2U,
        0
    );
    context.expect(
        firstAssigned.succeeded
            && first.mode() == RuntimeInternetLobbyFlowMode::Hosting
            && first.matchAssignment() != nullptr
            && first.matchAssignment()->hostPeerId == "match-first",
        "oldest compatible player should receive the host assignment first"
    );

    service->setNow(started + 3U);
    const RuntimeInternetLobbyFlowPollResult secondStillStaged = second.poll(
        started + 3U,
        0
    );
    context.expect(
        secondStillStaged.succeeded
            && second.mode() == RuntimeInternetLobbyFlowMode::Matchmaking
            && second.matchAssignment() == nullptr,
        "remote assignment must not release before room registration"
    );

    service->setNow(started + 4U);
    const RuntimeInternetLobbyFlowPollResult hostRegistered = first.poll(
        started + 4U,
        0
    );
    context.expect(
        hostRegistered.succeeded
            && first.mode() == RuntimeInternetLobbyFlowMode::Lobby,
        "assigned host should register the protected room before activation"
    );

    if (!pumpUntil(
            service,
            {&first, &second},
            [&first, &second]() {
                return first.gameplaySessionCount() == 1U
                    && second.gameplaySessionCount() == 1U
                    && first.lobbyFlow()->lobby().initialized()
                    && second.lobbyFlow()->lobby().initialized()
                    && first.lobbyFlow()->lobby().setup().members.size()
                        == 2U
                    && second.lobbyFlow()->lobby().setup().members.size()
                        == 2U;
            },
            context,
            "matchmaking assignment, proof admission, and lobby join"
        )) {
        return;
    }

    const RuntimeInternetMatchmakingAssignment* firstAssignment =
        first.matchAssignment();
    const RuntimeInternetMatchmakingAssignment* secondAssignment =
        second.matchAssignment();
    context.expect(
        firstAssignment && secondAssignment
            && firstAssignment->matchId == secondAssignment->matchId
            && firstAssignment->roomId == secondAssignment->roomId
            && secondAssignment->hostDisplayName
                == "match-first Commander"
            && second.lobbyFlow()->lobby().setup().members.front().displayName
                == "match-first Commander"
            && !secondAssignment->admissionProof.empty(),
        "both peers should retain one canonical match and per-peer admission proof"
    );
    context.expect(
        service->protectedCoordinatorSendCount() > 0U,
        "matched remote must present proof as authenticated coordinator metadata"
    );

    error.clear();
    context.expect(
        first.lobbyFlow()->verifyLocalMap(mapPath, error),
        "matched host map should verify: " + error
    );
    error.clear();
    context.expect(
        second.lobbyFlow()->verifyLocalMap(mapPath, error),
        "matched client map should verify: " + error
    );
    const RuntimeMultiplayerLanOperationResult firstReady =
        first.lobbyFlow()->setReady(true);
    const RuntimeMultiplayerLanOperationResult secondReady =
        second.lobbyFlow()->setReady(true);
    context.expect(
        firstReady.succeeded && secondReady.succeeded,
        "matched players should submit ready state"
    );
    if (!pumpUntil(
            service,
            {&first, &second},
            [&first]() {
                std::string reason;
                return first.lobbyFlow()->canStart(reason);
            },
            context,
            "matched ready consensus"
        )) {
        return;
    }
    const RuntimeMultiplayerLanOperationResult startResult =
        first.lobbyFlow()->startMatch(1);
    context.expect(
        startResult.succeeded,
        "matched host should start the game: " + startResult.reason
    );
    pumpUntil(
        service,
        {&first, &second},
        [&first, &second]() {
            return first.mode() == RuntimeInternetLobbyFlowMode::Running
                && second.mode() == RuntimeInternetLobbyFlowMode::Running;
        },
        context,
        "matched game start broadcast"
    );
    if (first.mode() != RuntimeInternetLobbyFlowMode::Running
        || second.mode() != RuntimeInternetLobbyFlowMode::Running) {
        return;
    }

    error.clear();
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> firstBridge =
        first.takeRunningBridge(error);
    context.expect(
        firstBridge != nullptr,
        "ranked host runtime handoff: " + error
    );
    error.clear();
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> secondBridge =
        second.takeRunningBridge(error);
    context.expect(
        secondBridge != nullptr,
        "ranked client runtime handoff: " + error
    );
    if (!firstBridge || !secondBridge) return;

    bool firstSubmitted = false;
    error.clear();
    context.expect(
        firstBridge->reportRankedMatchResult(
            true,
            firstSubmitted,
            error
        ) && firstSubmitted
            && service->communityStats().pendingQuickMatchReports == 1U,
        "winner report should remain pending for the opponent: " + error
    );
    bool secondSubmitted = false;
    error.clear();
    context.expect(
        secondBridge->reportRankedMatchResult(
            false,
            secondSubmitted,
            error
        ) && secondSubmitted
            && service->communityStats().pendingQuickMatchReports == 0U
            && service->communityStats().authorizedQuickMatches == 1U,
        "opposite reports should resolve the authorized ranked match: "
            + error
    );
    bool duplicateSubmitted = true;
    error.clear();
    context.expect(
        firstBridge->reportRankedMatchResult(
            false,
            duplicateSubmitted,
            error
        ) && !duplicateSubmitted
            && service->communityStats().pendingQuickMatchReports == 0U,
        "runtime bridge must not submit a ranked result twice"
    );
}

void testOrdinaryPasswordRoom(
    SmokeContext& context,
    std::uint64_t mapContentFingerprint
) {
    std::string error;
    std::shared_ptr<MemoryInternetService> service =
        MemoryInternetService::create(error);
    context.expect(service != nullptr, "password memory service: " + error);
    if (!service) return;

    RuntimeInternetRoomRecord room = roomRecord(
        "ordinary-password-room",
        "password-host",
        "Password Room"
    );
    room.passwordProtected = true;
    RuntimeInternetLobbyHostOptions hosting = hostOptions(
        "password-host",
        "Host",
        room,
        6101U,
        mapContentFingerprint,
        46101U
    );
    RuntimeRoomPasswordVerifier verifier = runtimeRoomPasswordVerifier(
        room.roomId,
        "correct horse battery staple"
    );
    hosting.roomPasswordVerifier.assign(verifier.begin(), verifier.end());

    RuntimeInternetLobbyFlow host(service->adapter("password-host"));
    const std::uint64_t started = nowMilliseconds();
    service->setNow(started);
    context.expect(
        host.startHost(std::move(hosting), started, error),
        "ordinary protected host should start: " + error
    );
    if (!pumpUntil(
            service,
            {&host},
            [&host]() {
                return host.mode() == RuntimeInternetLobbyFlowMode::Lobby;
            },
            context,
            "ordinary protected room registration"
        )) {
        runtimeRoomPasswordSecureClear(verifier);
        return;
    }
    context.expect(
        service->protectedCoordinatorSendCount() > 0U,
        "protected host registration must carry TLS-only verifier metadata"
    );

    constexpr std::uint64_t wrongTransaction = 6201U;
    RuntimeInternetLobbyJoinOptions wrongJoining = joinOptions(
        "wrong-password-peer",
        "Wrong",
        RuntimeInternetRole::Client,
        room,
        "Host",
        wrongTransaction,
        46102U
    );
    RuntimeRoomPasswordVerifier wrongVerifier = runtimeRoomPasswordVerifier(
        room.roomId,
        "wrong password"
    );
    RuntimeRoomPasswordProof wrongProof =
        runtimeInternetRoomPasswordJoinProof(
            wrongVerifier,
            room.roomId,
            "wrong-password-peer",
            wrongTransaction
        );
    wrongJoining.roomAdmissionProof.assign(
        wrongProof.begin(),
        wrongProof.end()
    );
    RuntimeInternetLobbyFlow wrong(
        service->adapter("wrong-password-peer")
    );
    error.clear();
    context.expect(
        wrong.join(std::move(wrongJoining), started, error),
        "wrong password request should reach coordinator proof check: " + error
    );
    const RuntimeInternetLobbyFlowPollResult denied = wrong.poll(started, 0);
    context.expect(
        !denied.succeeded
            && wrong.mode() == RuntimeInternetLobbyFlowMode::Failed
            && denied.error.find("password") != std::string::npos,
        "wrong Internet password proof must fail without opening a route"
    );

    constexpr std::uint64_t correctTransaction = 6301U;
    RuntimeInternetLobbyJoinOptions correctJoining = joinOptions(
        "correct-password-peer",
        "Correct",
        RuntimeInternetRole::Client,
        room,
        "Host",
        correctTransaction,
        46103U
    );
    RuntimeRoomPasswordProof correctProof =
        runtimeInternetRoomPasswordJoinProof(
            verifier,
            room.roomId,
            "correct-password-peer",
            correctTransaction
        );
    correctJoining.roomAdmissionProof.assign(
        correctProof.begin(),
        correctProof.end()
    );
    RuntimeInternetLobbyFlow correct(
        service->adapter("correct-password-peer")
    );
    error.clear();
    context.expect(
        correct.join(std::move(correctJoining), started, error),
        "correct protected join should start: " + error
    );
    const bool admitted = pumpUntil(
        service,
        {&host, &correct},
        [&host, &correct]() {
            return host.mode() == RuntimeInternetLobbyFlowMode::Lobby
                && correct.mode() == RuntimeInternetLobbyFlowMode::Lobby
                && host.gameplaySessionCount() == 1U
                && correct.gameplaySessionCount() == 1U
                && host.lobbyFlow()->lobby().setup().members.size() == 2U
                && correct.lobbyFlow()->lobby().setup().members.size() == 2U;
        },
        context,
        "correct ordinary Internet password admission"
    );
    context.expect(
        admitted && service->protectedCoordinatorSendCount() >= 2U,
        "protected registration and join should both use authenticated metadata"
    );
    runtimeRoomPasswordSecureClear(verifier);
    runtimeRoomPasswordSecureClear(wrongVerifier);
    runtimeRoomPasswordSecureClear(wrongProof);
    runtimeRoomPasswordSecureClear(correctProof);
    wrong.close();
    correct.close();
    host.close();
}

void testHostKick(
    SmokeContext& context,
    std::uint64_t mapContentFingerprint
) {
    std::string error;
    std::shared_ptr<MemoryInternetService> service =
        MemoryInternetService::create(error);
    context.expect(service != nullptr, "kick service should open: " + error);
    if (!service) return;
    const RuntimeInternetRoomRecord room = roomRecord(
        "kick-room",
        "kick-host",
        "Kick Lobby"
    );
    RuntimeInternetLobbyFlow host(service->adapter("kick-host"));
    RuntimeInternetLobbyFlow client(service->adapter("kick-client"));
    const std::uint64_t started = nowMilliseconds();
    service->setNow(started);
    context.expect(
        host.startHost(
            hostOptions(
                "kick-host",
                "Kick Host",
                room,
                9301,
                mapContentFingerprint,
                46201
            ),
            started,
            error
        ),
        "kick host should start: " + error
    );
    if (!pumpUntil(
            service,
            {&host},
            [&host]() {
                return host.mode() == RuntimeInternetLobbyFlowMode::Lobby;
            },
            context,
            "kick host registration"
        )) {
        return;
    }
    error.clear();
    context.expect(
        client.join(
            joinOptions(
                "kick-client",
                "Kick Client",
                RuntimeInternetRole::Client,
                room,
                "Kick Host",
                9302,
                46301
            ),
            nowMilliseconds(),
            error
        ),
        "kick client should join: " + error
    );
    if (!pumpUntil(
            service,
            {&host, &client},
            [&host, &client]() {
                return host.gameplaySessionCount() == 1U
                    && client.gameplaySessionCount() == 1U
                    && host.lobbyFlow()->lobby().setup().members.size()
                        == 2U;
            },
            context,
            "kick target admission"
        )) {
        return;
    }
    const RuntimeMultiplayerLanOperationResult kicked =
        host.lobbyFlow()->kickPeer("kick-client", "host kick smoke");
    context.expect(kicked.succeeded, "host should queue canonical kick");
    pumpUntil(
        service,
        {&host, &client},
        [&host, &client]() {
            return host.mode() == RuntimeInternetLobbyFlowMode::Lobby
                && client.mode() == RuntimeInternetLobbyFlowMode::Kicked
                && host.lobbyFlow()->lobby().setup().members.size() == 1U
                && host.routeCount() == 0U
                && host.gameplaySessionCount() == 0U;
        },
        context,
        "canonical Internet kick and route cleanup"
    );
}

void testWrongIdentity(SmokeContext& context) {
    std::string error;
    std::shared_ptr<MemoryInternetService> service =
        MemoryInternetService::create(error);
    context.expect(service != nullptr, "identity service should open: " + error);
    if (!service) return;
    RuntimeInternetLobbyFlow flow(service->adapter("authenticated-other"));
    RuntimeInternetLobbyQueryOptions query;
    query.connection = queryConfig("claimed-peer", 7001);
    context.expect(
        !flow.beginRoomQuery(query, nowMilliseconds(), error)
            && error.find("identity") != std::string::npos,
        "wrong authenticated adapter identity must be rejected before a query"
    );
}

void testLateRoomListTransactions(
    SmokeContext& context,
    std::uint64_t mapContentFingerprint
) {
    {
        std::string error;
        std::shared_ptr<MemoryInternetService> service =
            MemoryInternetService::create(error);
        context.expect(
            service != nullptr,
            "late query-to-host service should open: " + error
        );
        if (!service) return;

        constexpr std::uint64_t queryTransaction = 7101U;
        const std::string peerId = "late-query-host";
        RuntimeInternetLobbyFlow flow(service->adapter(peerId));
        RuntimeInternetLobbyQueryOptions query;
        query.connection = queryConfig(peerId, queryTransaction);
        const std::uint64_t started = nowMilliseconds();
        service->setNow(started);
        context.expect(
            flow.beginRoomQuery(query, started, error),
            "late query-to-host browse should start: " + error
        );
        RuntimeInternetLobbyFlowPollResult sent = flow.poll(started, 0);
        context.expect(
            sent.succeeded,
            "late query-to-host request should dispatch: " + sent.error
        );
        RuntimeInternetServiceEvent accepted;
        context.expect(
            service->takeCoordinatorEvent(
                peerId,
                RuntimeInternetControlType::RoomListResult,
                queryTransaction,
                accepted
            ),
            "late query-to-host fixture should capture the room-list result"
        );
        if (accepted.packet.payload.empty()) return;
        RuntimeInternetServiceEvent delayed = duplicateCoordinatorEvent(
            accepted
        );
        service->queueEvent(std::move(accepted));
        RuntimeInternetLobbyFlowPollResult completed = flow.poll(
            started + 1U,
            0
        );
        context.expect(
            completed.succeeded
                && flow.mode()
                    == RuntimeInternetLobbyFlowMode::RoomListReady,
            "first room-list snapshot should complete before hosting: "
                + completed.error
        );

        const RuntimeInternetRoomRecord hostedRoom = roomRecord(
            "late-query-host-room",
            peerId,
            "Late Query Host"
        );
        error.clear();
        context.expect(
            flow.startHost(
                hostOptions(
                    peerId,
                    "Late Query Host",
                    hostedRoom,
                    7102U,
                    mapContentFingerprint,
                    46411
                ),
                started + 2U,
                error
            ),
            "hosting after a completed query should start: " + error
        );
        service->queueEvent(std::move(delayed));
        RuntimeInternetLobbyFlowPollResult transition = flow.poll(
            started + 2U,
            0
        );
        context.expect(
            transition.succeeded
                && flow.mode() == RuntimeInternetLobbyFlowMode::Hosting,
            "late query A result should be ignored after switching to host: "
                + transition.error
        );
        pumpUntil(
            service,
            {&flow},
            [&flow]() {
                return flow.mode() == RuntimeInternetLobbyFlowMode::Lobby;
            },
            context,
            "query-to-host late room-list tombstone"
        );
    }

    {
        std::string error;
        std::shared_ptr<MemoryInternetService> service =
            MemoryInternetService::create(error);
        context.expect(
            service != nullptr,
            "late query-to-query service should open: " + error
        );
        if (!service) return;

        constexpr std::uint64_t firstTransaction = 7201U;
        constexpr std::uint64_t secondTransaction = 7202U;
        const std::string peerId = "late-query-browser";
        RuntimeInternetLobbyFlow flow(service->adapter(peerId));
        RuntimeInternetLobbyQueryOptions firstQuery;
        firstQuery.connection = queryConfig(peerId, firstTransaction);
        const std::uint64_t started = nowMilliseconds();
        service->setNow(started);
        context.expect(
            flow.beginRoomQuery(firstQuery, started, error),
            "query A should start: " + error
        );
        RuntimeInternetLobbyFlowPollResult sent = flow.poll(started, 0);
        context.expect(sent.succeeded, "query A should dispatch: " + sent.error);
        RuntimeInternetServiceEvent accepted;
        context.expect(
            service->takeCoordinatorEvent(
                peerId,
                RuntimeInternetControlType::RoomListResult,
                firstTransaction,
                accepted
            ),
            "query A result should be captured for delayed replay"
        );
        if (accepted.packet.payload.empty()) return;
        RuntimeInternetServiceEvent delayed = duplicateCoordinatorEvent(
            accepted
        );
        service->queueEvent(std::move(accepted));
        RuntimeInternetLobbyFlowPollResult completed = flow.poll(
            started + 1U,
            0
        );
        context.expect(
            completed.succeeded
                && flow.mode()
                    == RuntimeInternetLobbyFlowMode::RoomListReady,
            "query A should complete: " + completed.error
        );

        RuntimeInternetLobbyQueryOptions reused;
        reused.connection = queryConfig(peerId, firstTransaction);
        error.clear();
        context.expect(
            !flow.beginRoomQuery(reused, started + 2U, error)
                && error.find("recently completed") != std::string::npos,
            "a completed room-list transaction should remain tombstoned"
        );
        RuntimeInternetLobbyQueryOptions secondQuery;
        secondQuery.connection = queryConfig(peerId, secondTransaction);
        error.clear();
        context.expect(
            flow.beginRoomQuery(secondQuery, started + 3U, error),
            "query B should start with a fresh transaction: " + error
        );
        service->queueEvent(std::move(delayed));
        RuntimeInternetLobbyFlowPollResult transition = flow.poll(
            started + 3U,
            0
        );
        context.expect(
            transition.succeeded
                && flow.mode()
                    == RuntimeInternetLobbyFlowMode::QueryingRooms
                && !transition.roomsChanged,
            "late query A result should be ignored while query B is active: "
                + transition.error
        );
        pumpUntil(
            service,
            {&flow},
            [&flow]() {
                return flow.mode()
                    == RuntimeInternetLobbyFlowMode::RoomListReady;
            },
            context,
            "query A-to-B late room-list tombstone"
        );
    }
}

void testBoundButNotAdmitted(
    SmokeContext& context,
    std::uint64_t mapContentFingerprint
) {
    std::string error;
    std::shared_ptr<MemoryInternetService> service =
        MemoryInternetService::create(error);
    context.expect(service != nullptr, "rejection service should open: " + error);
    if (!service) return;
    const RuntimeInternetRoomRecord room = roomRecord(
        "rejection-room",
        "reject-host",
        "Admission Rejection"
    );
    RuntimeInternetLobbyFlow host(service->adapter("reject-host"));
    RuntimeInternetLobbyFlow rejected(service->adapter("rejected-peer"));
    const std::uint64_t now = nowMilliseconds();
    service->setNow(now);
    context.expect(
        host.startHost(
            hostOptions(
                "reject-host",
                "Reject Host",
                room,
                8001,
                mapContentFingerprint,
                44001
            ),
            now,
            error
        ),
        "rejection host should start: " + error
    );
    if (!pumpUntil(
            service,
            {&host},
            [&host]() {
                return host.mode() == RuntimeInternetLobbyFlowMode::Lobby;
            },
            context,
            "rejection host registration"
        )) {
        return;
    }
    RuntimeMultiplayerCompatibility incompatible = compatibility();
    incompatible.protocolFingerprint ^= 1ULL;
    error.clear();
    context.expect(
        rejected.join(
            joinOptions(
                "rejected-peer",
                "Rejected",
                RuntimeInternetRole::Client,
                room,
                "Reject Host",
                8002,
                45001,
                incompatible
            ),
            nowMilliseconds(),
            error
        ),
        "incompatible peer should reach authenticated admission: " + error
    );

    bool sawTunnelBound = false;
    bool sawJoinRejected = false;
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline
        && !sawJoinRejected) {
        const std::uint64_t tick = nowMilliseconds();
        service->setNow(tick);
        RuntimeInternetLobbyFlowPollResult hostPoll = host.poll(tick, 0);
        RuntimeInternetLobbyFlowPollResult rejectedPoll = rejected.poll(tick, 0);
        sawTunnelBound = sawTunnelBound
            || hasEvent(
                rejectedPoll,
                RuntimeInternetLobbyFlowEventKind::TcpTunnelBound
            );
        sawJoinRejected = hasEvent(
            rejectedPoll,
            RuntimeInternetLobbyFlowEventKind::JoinRejected
        );
        context.expect(
            hostPoll.succeeded && rejectedPoll.succeeded,
            "admission rejection should be a canonical lobby result: "
                + (!hostPoll.succeeded ? hostPoll.error : rejectedPoll.error)
        );
        if (!hostPoll.succeeded || !rejectedPoll.succeeded) break;
        if (!sawJoinRejected) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    context.expect(
        sawTunnelBound && sawJoinRejected,
        "TCP relay should bind before canonical compatibility rejection"
    );
    context.expect(
        rejected.gameplaySessionCount() == 0U
            && rejected.primaryConnectionState()
                == RuntimeInternetConnectionState::TcpTunnelBound,
        "a bound tunnel must not report gameplay joined without JoinAccepted"
    );
    context.expect(
        host.gameplaySessionCount() == 0U
            && host.lobbyFlow()->lobby().setup().members.size() == 1U,
        "host must not admit or report the rejected authenticated peer"
    );
}

void testCommunitySharesLobbyTransport(SmokeContext& context) {
    std::string error;
    const auto service = MemoryInternetService::create(error);
    context.expect(service != nullptr, "community memory service: " + error);
    if (!service) return;

    RuntimeInternetLobbyFlow alice(service->adapter("community-alice"));
    RuntimeInternetLobbyFlow bob(service->adapter("community-bob"));
    RuntimeInternetLobbyQueryOptions aliceQuery;
    aliceQuery.connection = queryConfig("community-alice", 8101U);
    RuntimeInternetLobbyQueryOptions bobQuery;
    bobQuery.connection = queryConfig("community-bob", 8102U);
    const std::uint64_t started = nowMilliseconds();
    context.expect(
        alice.beginRoomQuery(aliceQuery, started, error)
            && bob.beginRoomQuery(bobQuery, started, error),
        "community flows should begin normal room queries: " + error
    );
    context.expect(
        alice.requestCommunityLogin(8201U, error)
            && bob.requestCommunityLogin(8202U, error),
        "community login packets should share the coordinator channel: "
            + error
    );
    RuntimeInternetLobbyFlowPollResult alicePoll = alice.poll(started, 0);
    RuntimeInternetLobbyFlowPollResult bobPoll = bob.poll(started, 0);
    context.expect(
        alicePoll.succeeded && bobPoll.succeeded
            && alicePoll.communityChanged && bobPoll.communityChanged
            && !alice.communitySnapshot().registeredProfile
            && !bob.communitySnapshot().registeredProfile,
        "unregistered community snapshots should coexist with room polling"
    );

    context.expect(
        alice.requestCommunityRegistration(
            8203U,
            "FlowAlice",
            error
        ) && bob.requestCommunityRegistration(
            8204U,
            "FlowBob",
            error
        ),
        "community profiles should register through the flow: " + error
    );
    alicePoll = alice.poll(started + 1U, 0);
    bobPoll = bob.poll(started + 1U, 0);
    context.expect(
        alicePoll.succeeded && bobPoll.succeeded
            && alice.communitySnapshot().registeredProfile
            && bob.communitySnapshot().registeredProfile,
        "flow should retain registered community projections"
    );

    context.expect(
        alice.requestCommunityFriendAdd(8205U, "FlowBob", error)
            && alice.requestCommunityChannelJoin(8206U, "ra2", error)
            && bob.requestCommunityChannelJoin(8207U, "ra2", error),
        "Buddy and channel requests should share the live flow: " + error
    );
    alicePoll = alice.poll(started + 2U, 0);
    bobPoll = bob.poll(started + 2U, 0);
    context.expect(
        alicePoll.succeeded && bobPoll.succeeded
            && alice.communitySnapshot().friends.size() == 1U
            && alice.communitySnapshot().friends.front().peerId
                == "community-bob"
            && alice.communitySnapshot().selectedChannelId == "ra2"
            && bob.communitySnapshot().selectedChannelId == "ra2",
        "flow should project Buddy presence and selected channel"
    );

    context.expect(
        alice.requestCommunityChannelMessage(
            8208U,
            "Flow transport chat",
            error
        ),
        "flow chat request: " + error
    );
    alicePoll = alice.poll(started + 3U, 0);
    bobPoll = bob.poll(started + 3U, 0);
    context.expect(
        alicePoll.succeeded && bobPoll.succeeded
            && alicePoll.communityChanged && bobPoll.communityChanged
            && alice.communitySnapshot().chatHistory.size() == 1U
            && bob.communitySnapshot().chatHistory.size() == 1U
            && bob.communitySnapshot().chatHistory.front().text
                == "Flow transport chat",
        "community chat should be delivered without disturbing room state"
    );

    context.expect(
        alice.requestCommunityChannelJoin(8209U, "missing", error),
        "invalid channel request should still be a canonical application packet"
    );
    alicePoll = alice.poll(started + 4U, 0);
    context.expect(
        alicePoll.succeeded && alicePoll.communityChanged
            && std::any_of(
                alicePoll.communityEvents.begin(),
                alicePoll.communityEvents.end(),
                [](const RuntimeInternetCommunityClientEvent& event) {
                    return event.kind
                        == RuntimeInternetCommunityClientEventKind::OperationRejected;
                }
            ),
        "community application rejection should not tear down the lobby flow"
    );
}

} // namespace

int main() {
    SmokeContext context;
    const std::filesystem::path mapPath =
        std::filesystem::temp_directory_path()
        / ("concord_internet_lobby_flow_"
            + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            )
            + ".map");
    {
        std::ofstream output(mapPath, std::ios::binary | std::ios::trunc);
        output << "[Map]\nTheater=TEMPERATE\n[IsoMapPack5]\n1=fixture\n";
    }
    const std::uint64_t fingerprint = mapFingerprint(
        mapPath,
        "multimd/internet-lobby-flow.map",
        context
    );
    testHostClientSpectator(context, mapPath, fingerprint);
    testOrdinaryPasswordRoom(context, fingerprint);
    testMatchmakingToRunningLobby(context, mapPath, fingerprint);
    testHostKick(context, fingerprint);
    testWrongIdentity(context);
    testLateRoomListTransactions(context, fingerprint);
    testBoundButNotAdmitted(context, fingerprint);
    testCommunitySharesLobbyTransport(context);
    std::error_code ignored;
    std::filesystem::remove(mapPath, ignored);
    if (context.failures != 0) {
        std::cerr << context.failures
                  << " Runtime Internet lobby flow smoke checks failed\n";
        return 1;
    }
    std::cout << "Runtime Internet lobby flow smoke checks passed\n";
    return 0;
}
