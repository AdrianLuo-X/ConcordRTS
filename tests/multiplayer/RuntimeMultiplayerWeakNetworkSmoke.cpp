#include "network/lockstep/RuntimeMultiplayerLockstep.h"
#include "network/snapshots/RuntimeMultiplayerSnapshotTransfer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace {

struct TestContext {
    int failures = 0;

    bool expect(bool condition, const std::string& message) {
        if (condition) return true;
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
};

struct FaultProfile {
    const char* name = "clean";
    int latencySteps = 1;
    int jitterSteps = 0;
    std::uint64_t dropModulo = 0;
    std::uint64_t duplicateModulo = 0;
    int reorderSpan = 0;
    int outageStart = -1;
    int outageEnd = -1;
    std::uint64_t seed = 1;
};

struct FaultStats {
    std::uint64_t logicalMessages = 0;
    std::uint64_t deliveredPackets = 0;
    std::uint64_t droppedAttempts = 0;
    std::uint64_t retryAttempts = 0;
    std::uint64_t duplicatePackets = 0;
    std::uint64_t reorderSkews = 0;
    std::uint64_t outageRetries = 0;
    int maximumLatencySteps = 0;
};

template <typename Payload>
class DeterministicFaultPipe {
public:
    struct Delivery {
        std::uint64_t logicalId = 0;
        Payload payload;
    };

    explicit DeterministicFaultPipe(FaultProfile profile)
        : profile_(profile) {}

    void send(Payload payload, int now) {
        const std::uint64_t logicalId = nextLogicalId_++;
        ++stats_.logicalMessages;
        const std::uint64_t signature = logicalId * 0x9e3779b97f4a7c15ULL
            + profile_.seed * 0xbf58476d1ce4e5b9ULL;
        int due = now + profile_.latencySteps;
        if (profile_.jitterSteps > 0) {
            due += static_cast<int>(
                signature % static_cast<std::uint64_t>(
                    profile_.jitterSteps + 1
                )
            );
        }
        if (profile_.reorderSpan > 0 && (logicalId & 1U) != 0U) {
            due += profile_.reorderSpan;
            ++stats_.reorderSkews;
        }
        if (profile_.dropModulo > 0
            && (logicalId + profile_.seed) % profile_.dropModulo == 0U) {
            ++stats_.droppedAttempts;
            ++stats_.retryAttempts;
            due += std::max(2, profile_.latencySteps + 2);
        }
        if (profile_.outageStart >= 0
            && due >= profile_.outageStart && due < profile_.outageEnd) {
            ++stats_.droppedAttempts;
            ++stats_.retryAttempts;
            ++stats_.outageRetries;
            due = profile_.outageEnd
                + static_cast<int>(signature % 3U);
        }
        stats_.maximumLatencySteps = std::max(
            stats_.maximumLatencySteps,
            due - now
        );
        scheduled_.push_back(Scheduled{
            due,
            nextOrder_++,
            logicalId,
            payload
        });

        if (profile_.duplicateModulo > 0
            && logicalId % profile_.duplicateModulo == 0U) {
            ++stats_.duplicatePackets;
            const int duplicateDue = std::max(now, due - 1);
            scheduled_.push_back(Scheduled{
                duplicateDue,
                nextOrder_++,
                logicalId,
                std::move(payload)
            });
        }
    }

    std::vector<Delivery> takeDue(int now) {
        std::sort(
            scheduled_.begin(),
            scheduled_.end(),
            [](const Scheduled& left, const Scheduled& right) {
                return std::tie(left.due, left.order)
                    < std::tie(right.due, right.order);
            }
        );
        std::vector<Scheduled> remaining;
        remaining.reserve(scheduled_.size());
        for (Scheduled& item : scheduled_) {
            if (item.due <= now) {
                ++stats_.deliveredPackets;
                ready_.push_back(std::move(item));
            } else {
                remaining.push_back(std::move(item));
            }
        }
        scheduled_.swap(remaining);

        // Runtime multiplayer rides one reliable ordered TCP/TLS stream.
        // Faults happen below that stream: reordered packets and retransmits
        // must create head-of-line delay, never reorder lockstep messages.
        std::vector<Delivery> deliveries;
        const auto emitReadyId = [&](std::uint64_t logicalId) {
            for (auto iterator = ready_.begin(); iterator != ready_.end();) {
                if (iterator->logicalId == logicalId) {
                    deliveries.push_back(Delivery{
                        iterator->logicalId,
                        std::move(iterator->payload)
                    });
                    iterator = ready_.erase(iterator);
                } else {
                    ++iterator;
                }
            }
        };
        std::vector<std::uint64_t> lateDuplicateIds;
        for (const Scheduled& item : ready_) {
            if (item.logicalId < nextDeliveryId_) {
                lateDuplicateIds.push_back(item.logicalId);
            }
        }
        std::sort(lateDuplicateIds.begin(), lateDuplicateIds.end());
        lateDuplicateIds.erase(
            std::unique(lateDuplicateIds.begin(), lateDuplicateIds.end()),
            lateDuplicateIds.end()
        );
        for (const std::uint64_t logicalId : lateDuplicateIds) {
            emitReadyId(logicalId);
        }
        for (;;) {
            const bool haveNext = std::any_of(
                ready_.begin(),
                ready_.end(),
                [this](const Scheduled& item) {
                    return item.logicalId == nextDeliveryId_;
                }
            );
            if (!haveNext) break;
            emitReadyId(nextDeliveryId_);
            ++nextDeliveryId_;
        }
        return deliveries;
    }

    bool empty() const { return scheduled_.empty() && ready_.empty(); }
    const FaultStats& stats() const { return stats_; }

private:
    struct Scheduled {
        int due = 0;
        std::uint64_t order = 0;
        std::uint64_t logicalId = 0;
        Payload payload;
    };

    FaultProfile profile_;
    FaultStats stats_;
    std::uint64_t nextLogicalId_ = 1;
    std::uint64_t nextOrder_ = 1;
    std::uint64_t nextDeliveryId_ = 1;
    std::vector<Scheduled> scheduled_;
    std::vector<Scheduled> ready_;
};

enum PeerIndex : int {
    HostPeer = 0,
    ClientPeer = 1,
    SpectatorPeer = 2
};

const std::array<std::string, 3> peerIds{
    "host-peer",
    "client-peer",
    "spectator-peer"
};

struct DeterministicWorld {
    explicit DeterministicWorld(std::size_t unitCount = 256U)
        : units(unitCount) {
        for (std::size_t index = 0; index < units.size(); ++index) {
            units[index] = 0x100000001b3ULL
                ^ (static_cast<std::uint64_t>(index) * 0x9e3779b9ULL);
        }
    }

    bool apply(
        int tickValue,
        std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,
        std::string& errorMessage
    ) {
        if (tickValue != completedTick + 1 || units.empty()) {
            errorMessage = "world tick was not applied sequentially";
            return false;
        }
        for (const RuntimeMultiplayerLockstepApplyCommand& item : batch) {
            const auto* repair = std::get_if<
                RuntimeMultiplayerGameplayRepairCommand
            >(&item.command.body);
            if (repair == nullptr || repair->objectId < 0) {
                errorMessage = "weak-network fixture received an unknown command";
                return false;
            }
            const std::size_t index = static_cast<std::size_t>(
                repair->objectId
            ) % units.size();
            units[index] = units[index] * 6364136223846793005ULL
                + static_cast<std::uint64_t>(tickValue + 1)
                + item.frameSequence
                + static_cast<std::uint64_t>(item.commandIndex)
                + static_cast<std::uint64_t>(item.peerId.front());
            ++appliedCommands;
        }
        completedTick = tickValue;
        errorMessage.clear();
        return true;
    }

    std::uint64_t hash() const {
        std::uint64_t value = 1469598103934665603ULL;
        const auto mix = [&value](std::uint64_t item) {
            for (int shift = 56; shift >= 0; shift -= 8) {
                value ^= static_cast<std::uint8_t>(item >> shift);
                value *= 1099511628211ULL;
            }
        };
        mix(static_cast<std::uint64_t>(completedTick));
        mix(static_cast<std::uint64_t>(appliedCommands));
        for (const std::uint64_t unit : units) mix(unit);
        return value == 0 ? 1ULL : value;
    }

    std::vector<std::uint64_t> units;
    int completedTick = 0;
    std::uint64_t appliedCommands = 0;
};

RuntimeMultiplayerMatchSetup matchSetup(
    const RuntimeMultiplayerSessionState& session
) {
    RuntimeMultiplayerMatchSetup setup;
    setup.revision = 1;
    setup.hostPeerId = peerIds[HostPeer];
    setup.map.canonicalId = "maps/official/weak-network.map";
    setup.map.contentFingerprint =
        session.config().compatibility.contentFingerprint;
    setup.rules.gameMode = "battle";
    setup.rules.rulesOverride = "mpbattlemd.ini";
    setup.rules.randomSeed = 0x5eedc0decafef00dULL;
    setup.rules.fixedTickHz = 30;
    setup.rules.startingCredits = 10000;
    setup.rules.startingUnits = 10;
    setup.rules.techLevel = 10;
    setup.rules.gameSpeed = 2;

    for (const RuntimeMultiplayerRoomMember& roomMember : session.members()) {
        RuntimeMultiplayerMatchMember member;
        member.peerId = roomMember.peerId;
        member.displayName = roomMember.displayName;
        member.identity = roomMember.participation
                == RuntimeMultiplayerParticipationRole::Combatant
            ? RuntimeMultiplayerMatchMemberIdentity::Combatant
            : RuntimeMultiplayerMatchMemberIdentity::Spectator;
        setup.members.push_back(std::move(member));
        if (roomMember.participation
            == RuntimeMultiplayerParticipationRole::Spectator) {
            continue;
        }
        RuntimeMultiplayerMatchSlot slot;
        slot.slotIndex = static_cast<std::uint8_t>(setup.slots.size());
        slot.controller = RuntimeMultiplayerMatchSlotController::Human;
        slot.peerId = roomMember.peerId;
        slot.countryId = "random";
        slot.colorId = setup.slots.empty() ? "red" : "blue";
        slot.teamId = "-";
        slot.difficultyId = "medium";
        slot.startPosition = static_cast<std::uint8_t>(setup.slots.size() + 1U);
        setup.slots.push_back(std::move(slot));
    }
    return setup;
}

bool startSession(
    RuntimeMultiplayerSessionState& session,
    int stateHashPeriodTicks
) {
    RuntimeMultiplayerSessionConfig config;
    config.roomId = "weak-network-room";
    config.hostPeerId = peerIds[HostPeer];
    config.hostDisplayName = "Host";
    config.compatibility.protocolFingerprint = 0x1020304050607080ULL;
    config.compatibility.contentFingerprint = 0x8877665544332211ULL;
    config.stateHashPeriodTicks = stateHashPeriodTicks;
    config.stateHashLagWindowTicks = 120;
    if (!session.beginHost(config).accepted) return false;

    RuntimeMultiplayerJoinRequest client;
    client.peerId = peerIds[ClientPeer];
    client.displayName = "Client";
    client.compatibility = config.compatibility;
    if (!session.joinMember(client).accepted) return false;

    RuntimeMultiplayerJoinRequest spectator;
    spectator.peerId = peerIds[SpectatorPeer];
    spectator.displayName = "Spectator";
    spectator.participation = RuntimeMultiplayerParticipationRole::Spectator;
    spectator.compatibility = config.compatibility;
    if (!session.joinMember(spectator).accepted) return false;
    if (!session.setMatchSetup(peerIds[HostPeer], matchSetup(session)).accepted) {
        return false;
    }
    for (const std::string& peerId : peerIds) {
        if (!session.setMemberReady(peerId, true).accepted) return false;
    }
    return session.start(peerIds[HostPeer], 1).accepted;
}

struct Replica {
    RuntimeMultiplayerSessionState session;
    RuntimeMultiplayerLockstepDriver driver;
    DeterministicWorld world;
    std::set<int> pendingAdvances;
    std::set<int> countedDeferredAdvances;
    std::vector<RuntimeMultiplayerStateHashReport> pendingHashReports;

    explicit Replica(std::size_t units = 256U) : world(units) {}
};

bool initializeReplica(
    Replica& replica,
    int stateHashPeriodTicks = 1
) {
    RuntimeMultiplayerLockstepConfig lockstep;
    lockstep.inputDelayTicks = 0;
    return startSession(replica.session, stateHashPeriodTicks)
        && replica.driver.bind(replica.session, lockstep).accepted;
}

RuntimeMultiplayerCommandPacket commandPacket(
    int tick,
    int peerIndex,
    std::uint64_t sequence,
    std::size_t unitCount
) {
    RuntimeMultiplayerGameplayCommand command;
    const std::uint64_t object = static_cast<std::uint64_t>(tick) * 17U
        + static_cast<std::uint64_t>(peerIndex) * 101U;
    command.body = RuntimeMultiplayerGameplayRepairCommand{
        static_cast<std::int32_t>(object % unitCount) + 1
    };
    RuntimeMultiplayerGameplayFrame frame;
    frame.commands.push_back(std::move(command));
    RuntimeMultiplayerCommandPacket packet;
    packet.tick = tick;
    packet.peerId = peerIds[static_cast<std::size_t>(peerIndex)];
    packet.sequence = sequence;
    packet.timelineEpoch = 1;
    std::string error;
    if (!encodeRuntimeMultiplayerGameplayFrame(frame, packet.payload, error)) {
        packet.payload.clear();
    }
    return packet;
}

bool applyTick(Replica& replica, int tick, TestContext& context) {
    const RuntimeMultiplayerLockstepBatchCallback validate = [](
        std::span<const RuntimeMultiplayerLockstepApplyCommand>,
        std::string& errorMessage
    ) {
        errorMessage.clear();
        return true;
    };
    const RuntimeMultiplayerOperationResult applied =
        replica.driver.applyReadyTick(
            tick,
            validate,
            [&replica, tick](
                std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,
                std::string& errorMessage
            ) {
                return replica.world.apply(tick, batch, errorMessage);
            }
        );
    return context.expect(
        applied.accepted && applied.changed,
        "lockstep tick " + std::to_string(tick)
            + " did not apply: " + applied.reason
    );
}

struct AdvanceMessage {
    int completedTick = -1;
    std::uint64_t timelineEpoch = 1;
};

using GameBody = std::variant<
    RuntimeMultiplayerCommandPacket,
    AdvanceMessage,
    RuntimeMultiplayerStateHashReport
>;

struct GameWireMessage {
    int source = HostPeer;
    int destination = ClientPeer;
    GameBody body;
};

struct ProfileRunResult {
    bool passed = false;
    std::uint64_t authoredClientCommands = 0;
    std::uint64_t hostAcceptedClientCommands = 0;
    std::uint64_t deferredAdvances = 0;
    std::size_t snapshotBytes = 0;
    FaultStats gameStats;
    FaultStats snapshotStats;
};

Replica& replicaAt(std::array<Replica, 3>& replicas, int index) {
    return replicas[static_cast<std::size_t>(index)];
}

bool frameCompleteAt(const Replica& replica, int tick) {
    const auto found = std::find_if(
        replica.session.stateHashFrames().begin(),
        replica.session.stateHashFrames().end(),
        [tick](const RuntimeMultiplayerStateHashFrame& frame) {
            return frame.tick == tick;
        }
    );
    return found != replica.session.stateHashFrames().end()
        && found->complete && !found->desync;
}

struct SnapshotWireMessage {
    int source = HostPeer;
    int destination = ClientPeer;
    std::variant<
        RuntimeMultiplayerSnapshotChunk,
        RuntimeMultiplayerSnapshotChunkAck
    > body;
};

bool runSnapshotTransfer(
    const FaultProfile& profile,
    const RuntimeMultiplayerSessionState& session,
    std::uint64_t authoritativeHash,
    std::size_t& byteCount,
    FaultStats& stats,
    TestContext& context
) {
    std::vector<std::uint8_t> source(
        runtimeMultiplayerSnapshotChunkBytes * 2U + 137U
    );
    for (std::size_t index = 0; index < source.size(); ++index) {
        source[index] = static_cast<std::uint8_t>(
            (index * 131U + index / 17U + profile.seed) & 0xffU
        );
    }
    RuntimeMultiplayerSnapshotIdentity identity;
    identity.proposalId = 1;
    identity.kind = RuntimeMultiplayerSnapshotKind::Load;
    identity.sourceTimelineEpoch = session.timelineEpoch();
    identity.barrierTick = session.authoritativeTick() - 1;
    identity.snapshotTick = identity.barrierTick;
    identity.snapshotToken = std::string("weaknet-") + profile.name;
    identity.authoritativeStateHash = authoritativeHash;
    std::string error;
    if (session.matchSetup().has_value()) {
        (void)runtimeMultiplayerGameplaySetupFingerprint(
            *session.matchSetup(),
            identity.gameplaySetupFingerprint,
            error
        );
    }
    RuntimeMultiplayerSnapshotDescriptor descriptor;
    if (!context.expect(
            makeRuntimeMultiplayerSnapshotDescriptor(
                identity,
                source,
                descriptor,
                error
            ),
            std::string("snapshot descriptor failed: ") + error
        )) {
        return false;
    }

    std::array<RuntimeMultiplayerSnapshotChunkProducer, 2> producers;
    std::array<RuntimeMultiplayerSnapshotChunkAssembler, 2> assemblers;
    for (std::size_t index = 0; index < producers.size(); ++index) {
        if (!context.expect(
                producers[index].begin(descriptor, source).accepted
                    && assemblers[index].begin(descriptor).accepted,
                "snapshot endpoint did not initialize"
            )) {
            return false;
        }
    }

    DeterministicFaultPipe<SnapshotWireMessage> pipe(profile);
    std::array<std::set<std::uint64_t>, 3> deliveredLogicalIds;
    const auto sendCurrent = [&](std::size_t transferIndex, int now) {
        RuntimeMultiplayerSnapshotChunk chunk;
        std::string chunkError;
        if (!producers[transferIndex].currentChunk(chunk, chunkError)) {
            context.expect(false, "snapshot producer: " + chunkError);
            return;
        }
        pipe.send(
            SnapshotWireMessage{
                HostPeer,
                static_cast<int>(transferIndex) + 1,
                std::move(chunk)
            },
            now
        );
    };
    sendCurrent(0U, 0);
    sendCurrent(1U, 0);

    for (int now = 0; now < 10000; ++now) {
        for (auto& delivery : pipe.takeDue(now)) {
            const int destination = delivery.payload.destination;
            if (!deliveredLogicalIds[static_cast<std::size_t>(destination)]
                    .insert(delivery.logicalId).second) {
                continue;
            }
            if (const auto* chunk = std::get_if<
                    RuntimeMultiplayerSnapshotChunk
                >(&delivery.payload.body)) {
                const std::size_t transferIndex = static_cast<std::size_t>(
                    destination - 1
                );
                const RuntimeMultiplayerSnapshotTransferResult accepted =
                    assemblers[transferIndex].acceptChunk(*chunk);
                if (!context.expect(
                        accepted.accepted,
                        "faulted snapshot chunk was rejected: "
                            + accepted.reason
                    )) {
                    return false;
                }
                if (accepted.changed) {
                    const RuntimeMultiplayerSnapshotTransferResult duplicate =
                        assemblers[transferIndex].acceptChunk(*chunk);
                    if (!context.expect(
                            duplicate.accepted && duplicate.duplicate,
                            "identical snapshot duplicate was not idempotent"
                        )) {
                        return false;
                    }
                }
                RuntimeMultiplayerSnapshotChunkAck ack;
                error.clear();
                if (!context.expect(
                        assemblers[transferIndex].latestAcknowledgement(
                            ack,
                            error
                        ),
                        "snapshot acknowledgement failed: " + error
                    )) {
                    return false;
                }
                pipe.send(
                    SnapshotWireMessage{
                        destination,
                        HostPeer,
                        std::move(ack)
                    },
                    now
                );
            } else {
                const auto& ack = std::get<
                    RuntimeMultiplayerSnapshotChunkAck
                >(delivery.payload.body);
                const std::size_t transferIndex = static_cast<std::size_t>(
                    delivery.payload.source - 1
                );
                const RuntimeMultiplayerSnapshotTransferResult accepted =
                    producers[transferIndex].acknowledge(ack);
                if (!context.expect(
                        accepted.accepted,
                        "faulted snapshot acknowledgement was rejected: "
                            + accepted.reason
                    )) {
                    return false;
                }
                if (accepted.changed
                    && !producers[transferIndex].complete()) {
                    sendCurrent(transferIndex, now);
                }
            }
        }
        if (pipe.empty()
            && std::all_of(
                producers.begin(),
                producers.end(),
                [](const auto& producer) { return producer.complete(); }
            )) {
            break;
        }
        if (now == 9999) {
            context.expect(false, "faulted snapshot transfer timed out");
            return false;
        }
    }

    for (RuntimeMultiplayerSnapshotChunkAssembler& assembler : assemblers) {
        std::vector<std::uint8_t> completed;
        error.clear();
        if (!context.expect(
                assembler.takeCompletedBytes(completed, error)
                    && completed == source,
                "faulted snapshot payload changed: " + error
            )) {
            return false;
        }
    }
    byteCount = source.size();
    stats = pipe.stats();
    return true;
}

ProfileRunResult runWeakProfile(
    const FaultProfile& profile,
    TestContext& context
) {
    ProfileRunResult result;
    std::array<Replica, 3> replicas;
    for (Replica& replica : replicas) {
        if (!context.expect(
                initializeReplica(replica),
                std::string("could not initialize profile ") + profile.name
            )) {
            return result;
        }
    }

    const RuntimeMultiplayerCommandPacket forbidden = commandPacket(
        1,
        SpectatorPeer,
        1,
        replicas[0].world.units.size()
    );
    context.expect(
        !replicas[HostPeer].driver.submitFrame(forbidden).accepted,
        std::string(profile.name)
            + ": spectator gameplay command crossed the authority boundary"
    );

    DeterministicFaultPipe<GameWireMessage> pipe(profile);
    std::array<std::set<std::uint64_t>, 3> deliveredLogicalIds;
    constexpr int firstTick = 1;
    constexpr int tickCount = 72;
    constexpr int finalTick = firstTick + tickCount - 1;
    int nextAuthoredTick = firstTick;
    std::uint64_t hostSequence = 1;
    std::uint64_t clientSequence = 1;

    const auto broadcastCommand = [&](
        const RuntimeMultiplayerCommandPacket& packet,
        int now
    ) {
        for (const int destination : {ClientPeer, SpectatorPeer}) {
            pipe.send(
                GameWireMessage{HostPeer, destination, packet},
                now
            );
        }
    };
    const auto broadcastHash = [&](
        const RuntimeMultiplayerStateHashReport& report,
        int now
    ) {
        for (const int destination : {ClientPeer, SpectatorPeer}) {
            pipe.send(
                GameWireMessage{HostPeer, destination, report},
                now
            );
        }
    };
    const auto submitHostHash = [&](int completedTick, int now) {
        RuntimeMultiplayerStateHashReport report;
        report.tick = completedTick;
        report.peerId = peerIds[HostPeer];
        report.stateHash = replicas[HostPeer].world.hash();
        report.timelineEpoch = replicas[HostPeer].session.timelineEpoch();
        const RuntimeMultiplayerStateHashResult accepted =
            replicas[HostPeer].session.submitStateHash(report);
        if (!context.expect(
                accepted.accepted && !accepted.desync,
                std::string(profile.name) + ": host hash rejected: "
                    + accepted.reason
            )) {
            return false;
        }
        if (accepted.changed) broadcastHash(report, now);
        return true;
    };
    const auto submitRemoteHash = [&](int peerIndex, int completedTick, int now) {
        RuntimeMultiplayerStateHashReport report;
        report.tick = completedTick;
        report.peerId = peerIds[static_cast<std::size_t>(peerIndex)];
        report.stateHash = replicaAt(replicas, peerIndex).world.hash();
        report.timelineEpoch = replicaAt(replicas, peerIndex)
            .session.timelineEpoch();
        pipe.send(GameWireMessage{peerIndex, HostPeer, report}, now);
    };

    const auto acceptHash = [&](
        int destination,
        const RuntimeMultiplayerStateHashReport& report,
        int now
    ) {
        Replica& replica = replicaAt(replicas, destination);
        if (report.tick >= replica.session.authoritativeTick()) {
            replica.pendingHashReports.push_back(report);
            return true;
        }
        const RuntimeMultiplayerStateHashResult accepted =
            replica.session.submitStateHash(report);
        if (!context.expect(
                accepted.accepted && !accepted.desync,
                std::string(profile.name) + ": state hash rejected: "
                    + accepted.reason
            )) {
            return false;
        }
        if (destination == HostPeer && accepted.changed) {
            broadcastHash(report, now);
        }
        return true;
    };

    const auto drainReplicaHashes = [&](int peerIndex, int now) {
        Replica& replica = replicaAt(replicas, peerIndex);
        bool progressed = true;
        while (progressed) {
            progressed = false;
            for (auto iterator = replica.pendingHashReports.begin();
                 iterator != replica.pendingHashReports.end(); ++iterator) {
                if (iterator->tick >= replica.session.authoritativeTick()) {
                    continue;
                }
                const RuntimeMultiplayerStateHashReport report = *iterator;
                replica.pendingHashReports.erase(iterator);
                if (!acceptHash(peerIndex, report, now)) return false;
                progressed = true;
                break;
            }
        }
        return true;
    };

    const auto tryAdvanceRemote = [&](int peerIndex, int now) {
        Replica& replica = replicaAt(replicas, peerIndex);
        for (;;) {
            const int tick = replica.driver.currentBarrierTick();
            if (replica.pendingAdvances.find(tick)
                == replica.pendingAdvances.end()) {
                return true;
            }
            if (!replica.driver.frameBarrierReady(tick)
                || !replica.driver.inputHorizonReady()) {
                if (replica.countedDeferredAdvances.insert(tick).second) {
                    ++result.deferredAdvances;
                }
                return true;
            }
            if (!applyTick(replica, tick, context)) return false;
            const RuntimeMultiplayerOperationResult advanced =
                replica.session.advanceAuthoritativeTick(
                    peerIds[HostPeer],
                    tick + 1,
                    replica.session.timelineEpoch()
                );
            const RuntimeMultiplayerOperationResult acknowledged =
                replica.driver.acknowledgeAuthoritativeAdvance(tick);
            if (!context.expect(
                    advanced.accepted && acknowledged.accepted,
                    std::string(profile.name)
                        + ": replicated advance failed at tick "
                        + std::to_string(tick)
                )) {
                return false;
            }
            replica.pendingAdvances.erase(tick);
            if (!drainReplicaHashes(peerIndex, now)) return false;
            submitRemoteHash(peerIndex, tick, now);
        }
    };

    for (int now = 0; now < 20000; ++now) {
        if (nextAuthoredTick <= finalTick
            && nextAuthoredTick
                <= replicas[HostPeer].driver.currentBarrierTick() + 8
            && (now % 2) == 0) {
            const RuntimeMultiplayerCommandPacket hostPacket = commandPacket(
                nextAuthoredTick,
                HostPeer,
                hostSequence++,
                replicas[HostPeer].world.units.size()
            );
            const RuntimeMultiplayerOperationResult hostAccepted =
                replicas[HostPeer].driver.submitFrame(hostPacket);
            if (!context.expect(
                    hostAccepted.accepted && hostAccepted.changed,
                    std::string(profile.name)
                        + ": host command was not accepted"
                )) {
                return result;
            }
            broadcastCommand(hostPacket, now);

            RuntimeMultiplayerCommandPacket clientPacket = commandPacket(
                nextAuthoredTick,
                ClientPeer,
                clientSequence++,
                replicas[HostPeer].world.units.size()
            );
            ++result.authoredClientCommands;
            pipe.send(
                GameWireMessage{
                    ClientPeer,
                    HostPeer,
                    std::move(clientPacket)
                },
                now
            );
            ++nextAuthoredTick;
        }

        for (auto& delivery : pipe.takeDue(now)) {
            const int destination = delivery.payload.destination;
            if (!deliveredLogicalIds[static_cast<std::size_t>(destination)]
                    .insert(delivery.logicalId).second) {
                continue;
            }
            if (const auto* packet = std::get_if<
                    RuntimeMultiplayerCommandPacket
                >(&delivery.payload.body)) {
                RuntimeMultiplayerOperationResult accepted =
                    replicaAt(replicas, destination).driver.submitFrame(*packet);
                if (!context.expect(
                        accepted.accepted,
                        std::string(profile.name) + ": command rejected: "
                            + accepted.reason
                    )) {
                    return result;
                }
                if (destination == HostPeer && accepted.changed) {
                    ++result.hostAcceptedClientCommands;
                    broadcastCommand(*packet, now);
                }
            } else if (const auto* advance = std::get_if<AdvanceMessage>(
                    &delivery.payload.body
                )) {
                if (destination == HostPeer
                    || advance->timelineEpoch
                        != replicaAt(replicas, destination)
                            .session.timelineEpoch()) {
                    context.expect(false, "invalid faulted advance envelope");
                    return result;
                }
                Replica& replica = replicaAt(replicas, destination);
                if (advance->completedTick
                    >= replica.driver.currentBarrierTick()) {
                    replica.pendingAdvances.insert(advance->completedTick);
                }
            } else {
                if (!acceptHash(
                        destination,
                        std::get<RuntimeMultiplayerStateHashReport>(
                            delivery.payload.body
                        ),
                        now
                    )) {
                    return result;
                }
            }
        }

        for (const int peerIndex : {ClientPeer, SpectatorPeer}) {
            if (!tryAdvanceRemote(peerIndex, now)) return result;
        }

        Replica& host = replicas[HostPeer];
        while (host.driver.currentBarrierTick() <= finalTick
            && host.driver.frameBarrierReady(host.driver.currentBarrierTick())
            && host.driver.inputHorizonReady()) {
            const int tick = host.driver.currentBarrierTick();
            if (!applyTick(host, tick, context)) return result;
            const RuntimeMultiplayerOperationResult advanced =
                host.driver.advanceHost(peerIds[HostPeer], tick);
            if (!context.expect(
                    advanced.accepted,
                    std::string(profile.name) + ": host advance failed: "
                        + advanced.reason
                ) || !submitHostHash(tick, now)) {
                return result;
            }
            for (const int destination : {ClientPeer, SpectatorPeer}) {
                pipe.send(
                    GameWireMessage{
                        HostPeer,
                        destination,
                        AdvanceMessage{tick, host.session.timelineEpoch()}
                    },
                    now
                );
            }
        }

        const bool worldsComplete = std::all_of(
            replicas.begin(),
            replicas.end(),
            [](const Replica& replica) {
                return replica.world.completedTick == finalTick;
            }
        );
        if (nextAuthoredTick > finalTick && worldsComplete && pipe.empty()) {
            bool hashesDrained = true;
            for (const Replica& replica : replicas) {
                hashesDrained = hashesDrained
                    && replica.pendingHashReports.empty()
                    && frameCompleteAt(replica, finalTick);
            }
            if (hashesDrained) break;
        }
        if (now == 19999) {
            context.expect(
                false,
                std::string(profile.name) + ": weak-network run timed out"
            );
            return result;
        }
    }

    const std::uint64_t expectedCommands =
        static_cast<std::uint64_t>(tickCount) * 2U;
    const std::uint64_t worldHash = replicas[HostPeer].world.hash();
    const std::uint64_t sessionHash = replicas[HostPeer].session.stateHash();
    const std::uint64_t driverHash = replicas[HostPeer].driver.stateHash();
    bool equal = result.authoredClientCommands == tickCount
        && result.hostAcceptedClientCommands == tickCount;
    for (const Replica& replica : replicas) {
        equal = equal
            && replica.world.completedTick == finalTick
            && replica.world.appliedCommands == expectedCommands
            && replica.world.hash() == worldHash
            && replica.session.stateHash() == sessionHash
            && replica.driver.stateHash() == driverHash
            && frameCompleteAt(replica, finalTick)
            && replica.session.firstDesyncTick() < 0;
    }
    if (!context.expect(
            equal,
            std::string(profile.name)
                + ": a command, tick, or hash diverged silently"
        )) {
        return result;
    }

    result.gameStats = pipe.stats();
    if (!runSnapshotTransfer(
            profile,
            replicas[HostPeer].session,
            worldHash,
            result.snapshotBytes,
            result.snapshotStats,
            context
        )) {
        return result;
    }
    result.passed = true;
    return result;
}

bool runLongGame(TestContext& context) {
    constexpr int firstTick = 1;
    constexpr int tickCount = 30 * 60 * 30;
    constexpr int finalTick = firstTick + tickCount - 1;
    constexpr std::size_t unitCount = 4096U;
    std::array<Replica, 3> replicas{
        Replica(unitCount),
        Replica(unitCount),
        Replica(unitCount)
    };
    for (Replica& replica : replicas) {
        if (!context.expect(
                initializeReplica(replica, 300),
                "long-game replica did not initialize"
            )) {
            return false;
        }
    }

    const auto started = std::chrono::steady_clock::now();
    for (int tick = firstTick; tick <= finalTick; ++tick) {
        const RuntimeMultiplayerCommandPacket hostPacket = commandPacket(
            tick,
            HostPeer,
            static_cast<std::uint64_t>(tick),
            unitCount
        );
        const RuntimeMultiplayerCommandPacket clientPacket = commandPacket(
            tick,
            ClientPeer,
            static_cast<std::uint64_t>(tick),
            unitCount
        );
        for (std::size_t index = 0; index < replicas.size(); ++index) {
            RuntimeMultiplayerOperationResult first;
            RuntimeMultiplayerOperationResult second;
            if (((tick + static_cast<int>(index)) & 1) == 0) {
                first = replicas[index].driver.submitFrame(hostPacket);
                second = replicas[index].driver.submitFrame(clientPacket);
            } else {
                first = replicas[index].driver.submitFrame(clientPacket);
                second = replicas[index].driver.submitFrame(hostPacket);
            }
            if (!context.expect(
                    first.accepted && first.changed
                        && second.accepted && second.changed,
                    "long-game command admission failed at tick "
                        + std::to_string(tick) + ": "
                        + (!first.accepted ? first.reason : second.reason)
                ) || !applyTick(replicas[index], tick, context)) {
                return false;
            }
        }
        if (!context.expect(
                replicas[HostPeer].driver.advanceHost(
                    peerIds[HostPeer],
                    tick
                ).accepted,
                "long-game host advance failed at tick "
                    + std::to_string(tick)
            )) {
            return false;
        }
        for (const int peerIndex : {ClientPeer, SpectatorPeer}) {
            Replica& replica = replicaAt(replicas, peerIndex);
            if (!context.expect(
                    replica.session.advanceAuthoritativeTick(
                        peerIds[HostPeer],
                        tick + 1,
                        replica.session.timelineEpoch()
                    ).accepted
                        && replica.driver
                            .acknowledgeAuthoritativeAdvance(tick).accepted,
                    "long-game replicated advance failed at tick "
                        + std::to_string(tick)
                )) {
                return false;
            }
        }

        if ((tick - firstTick) % 300 == 0 || tick == finalTick) {
            const std::uint64_t expectedHash = replicas[0].world.hash();
            if (!context.expect(
                    replicas[1].world.hash() == expectedHash
                        && replicas[2].world.hash() == expectedHash,
                    "long-game world hash diverged at tick "
                        + std::to_string(tick)
                )) {
                return false;
            }
            for (std::size_t replicaIndex = 0;
                 replicaIndex < replicas.size(); ++replicaIndex) {
                for (int offset = 0; offset < 3; ++offset) {
                    const int reportPeer = (
                        offset + static_cast<int>(replicaIndex)
                    ) % 3;
                    RuntimeMultiplayerStateHashReport report;
                    report.tick = tick;
                    report.peerId = peerIds[
                        static_cast<std::size_t>(reportPeer)
                    ];
                    report.stateHash = expectedHash;
                    report.timelineEpoch =
                        replicas[replicaIndex].session.timelineEpoch();
                    const RuntimeMultiplayerStateHashResult accepted =
                        replicas[replicaIndex].session.submitStateHash(report);
                    if (!context.expect(
                            accepted.accepted && !accepted.desync,
                            "long-game hash consensus failed at tick "
                                + std::to_string(tick)
                        )) {
                        return false;
                    }
                }
            }
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started
    ).count();
    const std::uint64_t finalWorldHash = replicas[0].world.hash();
    const std::uint64_t finalSessionHash = replicas[0].session.stateHash();
    const std::uint64_t finalDriverHash = replicas[0].driver.stateHash();
    bool equal = true;
    for (const Replica& replica : replicas) {
        equal = equal
            && replica.world.completedTick == finalTick
            && replica.world.units.size() == unitCount
            && replica.world.appliedCommands
                == static_cast<std::uint64_t>(tickCount) * 2U
            && replica.world.hash() == finalWorldHash
            && replica.session.stateHash() == finalSessionHash
            && replica.driver.stateHash() == finalDriverHash
            && replica.session.firstDesyncTick() < 0;
    }
    context.expect(equal, "30-minute simulated long game diverged");
    std::cout << "LONG_GAME simulatedSeconds=1800 ticks=" << tickCount
              << " units=" << unitCount
              << " appliedCommands=" << tickCount * 2
              << " wallMilliseconds=" << elapsed
              << " finalHash=" << finalWorldHash
              << " result=" << (equal ? "PASS" : "FAIL") << '\n';
    return equal;
}

} // namespace

int main() {
    TestContext context;
    const std::array<FaultProfile, 7> profiles{{
        {"latency", 6, 0, 0, 0, 0, -1, -1, 0x11U},
        {"jitter", 2, 7, 0, 0, 0, -1, -1, 0x22U},
        {"loss", 2, 3, 4, 0, 0, -1, -1, 0x33U},
        {"duplicate", 2, 2, 0, 3, 0, -1, -1, 0x44U},
        {"reorder", 2, 4, 0, 0, 7, -1, -1, 0x55U},
        {"disconnect", 2, 3, 0, 0, 4, 10, 40, 0x66U},
        {"combined", 3, 6, 5, 4, 8, 12, 48, 0x77U}
    }};

    std::cout << "NETWORK_MATRIX_BEGIN stepMilliseconds=10 seedPolicy=fixed\n";
    for (const FaultProfile& profile : profiles) {
        const ProfileRunResult result = runWeakProfile(profile, context);
        const FaultStats total{
            result.gameStats.logicalMessages
                + result.snapshotStats.logicalMessages,
            result.gameStats.deliveredPackets
                + result.snapshotStats.deliveredPackets,
            result.gameStats.droppedAttempts
                + result.snapshotStats.droppedAttempts,
            result.gameStats.retryAttempts
                + result.snapshotStats.retryAttempts,
            result.gameStats.duplicatePackets
                + result.snapshotStats.duplicatePackets,
            result.gameStats.reorderSkews
                + result.snapshotStats.reorderSkews,
            result.gameStats.outageRetries
                + result.snapshotStats.outageRetries,
            std::max(
                result.gameStats.maximumLatencySteps,
                result.snapshotStats.maximumLatencySteps
            )
        };
        std::cout << "NETWORK_PROFILE name=" << profile.name
                  << " latencySteps=" << profile.latencySteps
                  << " jitterSteps=" << profile.jitterSteps
                  << " lossEvery=" << profile.dropModulo
                  << " duplicateEvery=" << profile.duplicateModulo
                  << " reorderSpan=" << profile.reorderSpan
                  << " outage=" << profile.outageStart << ':'
                  << profile.outageEnd
                  << " drops=" << total.droppedAttempts
                  << " retries=" << total.retryAttempts
                  << " duplicates=" << total.duplicatePackets
                  << " reorderSkews=" << total.reorderSkews
                  << " outageRetries=" << total.outageRetries
                  << " maxLatencySteps=" << total.maximumLatencySteps
                  << " authoredClient=" << result.authoredClientCommands
                  << " acceptedClient="
                  << result.hostAcceptedClientCommands
                  << " deferredAdvances=" << result.deferredAdvances
                  << " snapshotBytes=" << result.snapshotBytes
                  << " result=" << (result.passed ? "PASS" : "FAIL")
                  << '\n';
    }
    std::cout << "NETWORK_MATRIX_END\n";
    (void)runLongGame(context);

    if (context.failures != 0) {
        std::cerr << "Runtime multiplayer weak-network smoke failed with "
                  << context.failures << " failure(s).\n";
        return 1;
    }
    std::cout << "Runtime multiplayer weak-network smoke passed.\n";
    return 0;
}
