#include "network/session/RuntimeMultiplayerLanSession.h"
#include "network/lockstep/RuntimeMultiplayerLockstep.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
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

RuntimeMultiplayerSessionConfig sessionConfig() {
    RuntimeMultiplayerSessionConfig config;
    config.roomId = "lan-flow-room";
    config.hostPeerId = "host-peer";
    config.hostDisplayName = "Host Player";
    config.compatibility.protocolFingerprint = 0x1020304050607080ULL;
    config.compatibility.contentFingerprint = 0x8877665544332211ULL;
    config.stateHashPeriodTicks = 1;
    return config;
}

RuntimeMultiplayerMatchSetup matchSetupFor(
    const RuntimeMultiplayerSessionState& session,
    std::uint64_t revision,
    const std::string& mapId
) {
    RuntimeMultiplayerMatchSetup setup;
    setup.revision = revision;
    setup.hostPeerId = session.config().hostPeerId;
    setup.map.canonicalId = mapId;
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
    for (const RuntimeMultiplayerRoomMember& roomMember : session.members()) {
        RuntimeMultiplayerMatchMember setupMember;
        setupMember.peerId = roomMember.peerId;
        setupMember.displayName = roomMember.displayName;
        setupMember.identity = roomMember.participation
                == RuntimeMultiplayerParticipationRole::Combatant
            ? RuntimeMultiplayerMatchMemberIdentity::Combatant
            : RuntimeMultiplayerMatchMemberIdentity::Spectator;
        setup.members.push_back(std::move(setupMember));

        if (roomMember.participation
            == RuntimeMultiplayerParticipationRole::Spectator) {
            continue;
        }
        RuntimeMultiplayerMatchSlot slot;
        slot.slotIndex = slotIndex;
        slot.controller = RuntimeMultiplayerMatchSlotController::Human;
        slot.peerId = roomMember.peerId;
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

bool pumpUntil(
    RuntimeMultiplayerLanSession& host,
    RuntimeMultiplayerLanSession& client,
    const std::function<bool()>& complete,
    SmokeContext& context,
    const std::string& description,
    int timeoutMilliseconds = 5000
) {
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMilliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const RuntimeMultiplayerLanPollResult hostPoll = host.poll(0);
        const RuntimeMultiplayerLanPollResult clientPoll = client.poll(0);
        if (!hostPoll.succeeded) {
            context.expect(
                false,
                description + ": host poll failed: " + hostPoll.error
            );
            return false;
        }
        if (!clientPoll.succeeded) {
            context.expect(
                false,
                description + ": client poll failed: " + clientPoll.error
            );
            return false;
        }
        if (complete()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(false, description + ": timed out");
    return false;
}

RuntimeMultiplayerGameplayCommand stopCommand(
    std::vector<std::int32_t> actorIds
) {
    RuntimeMultiplayerGameplayCommand command;
    RuntimeMultiplayerGameplayStopCommand stop;
    stop.objectIds = std::move(actorIds);
    command.body = std::move(stop);
    return command;
}

std::vector<std::uint8_t> encodedGameplayFrame(
    RuntimeMultiplayerGameplayFrame frame,
    SmokeContext& context
) {
    std::vector<std::uint8_t> payload;
    std::string error;
    context.expect(
        encodeRuntimeMultiplayerGameplayFrame(frame, payload, error),
        "gameplay frame should encode: " + error
    );
    return payload;
}

RuntimeMultiplayerLanCommandPayloadValidator gameplayFrameValidator() {
    return [](
        const RuntimeMultiplayerCommandPacket& packet,
        std::string& errorMessage
    ) {
        RuntimeMultiplayerGameplayFrame frame;
        return decodeRuntimeMultiplayerGameplayFrame(
            packet.payload,
            frame,
            errorMessage
        );
    };
}

struct SnapshotHookProbe {
    int stages = 0;
    int commits = 0;
    int postCommits = 0;
    int discards = 0;
    bool failStage = false;
    bool failCommit = false;
    bool failPostCommit = false;
    std::vector<std::uint8_t> stagedBytes;
    std::uint64_t postEpoch = 0;
    int postTick = -1;
    RuntimeMultiplayerLockstepDriver* lockstepDriver = nullptr;
};

RuntimeMultiplayerLanSnapshotHooks snapshotHooks(
    SnapshotHookProbe& probe
) {
    RuntimeMultiplayerLanSnapshotHooks hooks;
    hooks.stageVerifiedBytes = [&probe](
        const RuntimeMultiplayerSnapshotDescriptor& descriptor,
        std::span<const std::uint8_t> bytes,
        std::uint64_t& observedHash,
        std::string& errorMessage
    ) {
        ++probe.stages;
        if (probe.failStage) {
            errorMessage = "fixture snapshot stage failed";
            return false;
        }
        probe.stagedBytes.assign(bytes.begin(), bytes.end());
        observedHash = descriptor.identity.authoritativeStateHash;
        return true;
    };
    hooks.commitStaged = [&probe](
        const RuntimeMultiplayerSnapshotDescriptor& descriptor,
        std::uint64_t& observedHash,
        std::string& errorMessage
    ) {
        ++probe.commits;
        if (probe.failCommit) {
            errorMessage = "fixture snapshot commit failed";
            return false;
        }
        if (probe.stagedBytes.empty()) {
            errorMessage = "fixture has no staged snapshot";
            return false;
        }
        observedHash = descriptor.identity.authoritativeStateHash;
        return true;
    };
    hooks.postSessionCommit = [&probe](
        const RuntimeMultiplayerSnapshotDescriptor&,
        const RuntimeMultiplayerSessionState& session,
        std::string& errorMessage
    ) {
        ++probe.postCommits;
        probe.postEpoch = session.timelineEpoch();
        probe.postTick = session.authoritativeTick();
        if (probe.failPostCommit) {
            errorMessage = "fixture snapshot post-commit failed";
            return false;
        }
        if (probe.lockstepDriver != nullptr) {
            const RuntimeMultiplayerOperationResult rebound =
                probe.lockstepDriver->rebindAfterLoadCommit(session);
            if (!rebound.accepted) {
                errorMessage = rebound.reason;
                return false;
            }
        }
        return true;
    };
    hooks.discardStaged = [&probe](
        const RuntimeMultiplayerSnapshotDescriptor&
    ) {
        ++probe.discards;
        probe.stagedBytes.clear();
    };
    return hooks;
}

RuntimeMultiplayerSaveLoadProposal snapshotProposal(
    const RuntimeMultiplayerSessionState& session,
    std::uint64_t proposalId,
    RuntimeMultiplayerSaveLoadKind kind,
    int snapshotTick,
    const std::string& token,
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
        "snapshot gameplay setup fingerprint should compute: " + error
    );
    RuntimeMultiplayerSnapshotIdentity identity;
    identity.proposalId = proposalId;
    identity.kind = kind == RuntimeMultiplayerSaveLoadKind::Save
        ? RuntimeMultiplayerSnapshotKind::Save
        : RuntimeMultiplayerSnapshotKind::Load;
    identity.sourceTimelineEpoch = session.timelineEpoch();
    identity.barrierTick = session.authoritativeTick() - 1;
    identity.snapshotTick = snapshotTick;
    identity.snapshotToken = token;
    identity.authoritativeStateHash =
        0x8f3a0c759d2e4100ULL ^ proposalId;
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
        "snapshot descriptor should be canonical: " + error
    );
    RuntimeMultiplayerSaveLoadProposal proposal;
    proposal.timelineEpoch = session.timelineEpoch();
    proposal.proposalId = proposalId;
    proposal.kind = kind;
    proposal.proposerPeerId = session.config().hostPeerId;
    proposal.descriptor = std::move(descriptor);
    proposal.tick = snapshotTick;
    proposal.snapshotId = token;
    proposal.snapshotFingerprint =
        proposal.descriptor.snapshotFingerprint;
    return proposal;
}

struct SimulationHookProbe {
    int starts = 0;
    int commands = 0;
    int prepareCalls = 0;
    int applyCalls = 0;
    int acknowledgements = 0;
    int deferredAdvanceAttempts = 0;
    bool failNextApply = false;
    std::vector<std::string> order;
};

RuntimeMultiplayerLanSimulationHooks lockstepSimulationHooks(
    RuntimeMultiplayerLockstepDriver& driver,
    SimulationHookProbe& probe
) {
    RuntimeMultiplayerLanSimulationHooks hooks;
    hooks.onStarted = [&driver, &probe](
        const RuntimeMultiplayerSessionState& session,
        std::string& errorMessage
    ) {
        ++probe.starts;
        probe.order.push_back("start");
        RuntimeMultiplayerLockstepConfig config;
        config.inputDelayTicks = 0;
        const RuntimeMultiplayerOperationResult bound =
            driver.bind(session, config);
        if (!bound.accepted) errorMessage = bound.reason;
        return bound.accepted;
    };
    hooks.onCommandAdmitted = [&driver, &probe](
        const RuntimeMultiplayerCommandPacket& packet,
        std::string& errorMessage
    ) {
        ++probe.commands;
        probe.order.push_back(
            "command:" + packet.peerId + ":"
                + std::to_string(packet.sequence)
        );
        const RuntimeMultiplayerOperationResult observed =
            driver.observeAdmittedFrame(packet);
        if (!observed.accepted) errorMessage = observed.reason;
        return observed.accepted;
    };
    hooks.prepareAuthoritativeAdvance = [&driver, &probe](
        int completedTick,
        std::string& errorMessage
    ) {
        ++probe.prepareCalls;
        probe.order.push_back(
            "prepare:" + std::to_string(completedTick)
        );
        if (completedTick != driver.currentBarrierTick()) {
            errorMessage = "simulation hook barrier tick mismatch";
            return RuntimeMultiplayerLanSimulationAdvanceDecision::Rejected;
        }
        if (!driver.tickCompleted(completedTick)) {
            if (!driver.frameBarrierReady(completedTick)) {
                return RuntimeMultiplayerLanSimulationAdvanceDecision::Deferred;
            }
            const bool failApply = probe.failNextApply;
            probe.failNextApply = false;
            const RuntimeMultiplayerLockstepBatchCallback prevalidate = [](
                std::span<const RuntimeMultiplayerLockstepApplyCommand>,
                std::string&
            ) {
                return true;
            };
            const RuntimeMultiplayerLockstepBatchCallback apply = [
                &probe,
                failApply
            ](
                std::span<const RuntimeMultiplayerLockstepApplyCommand>,
                std::string& callbackError
            ) {
                ++probe.applyCalls;
                if (failApply) {
                    callbackError = "fixture world apply failed";
                    return false;
                }
                return true;
            };
            const RuntimeMultiplayerOperationResult applied =
                driver.applyReadyTick(
                    completedTick,
                    prevalidate,
                    apply
                );
            if (!applied.accepted) {
                errorMessage = applied.reason;
                return RuntimeMultiplayerLanSimulationAdvanceDecision::Rejected;
            }
        }
        if (!driver.inputHorizonReady()) {
            return RuntimeMultiplayerLanSimulationAdvanceDecision::Deferred;
        }
        if (probe.deferredAdvanceAttempts > 0) {
            --probe.deferredAdvanceAttempts;
            return RuntimeMultiplayerLanSimulationAdvanceDecision::Deferred;
        }
        return RuntimeMultiplayerLanSimulationAdvanceDecision::Ready;
    };
    hooks.onAuthoritativeAdvance = [&driver, &probe](
        int completedTick,
        std::string& errorMessage
    ) {
        ++probe.acknowledgements;
        probe.order.push_back(
            "ack:" + std::to_string(completedTick)
        );
        const RuntimeMultiplayerOperationResult acknowledged =
            driver.acknowledgeAuthoritativeAdvance(completedTick);
        if (!acknowledged.accepted) errorMessage = acknowledged.reason;
        return acknowledged.accepted;
    };
    return hooks;
}

RuntimeMultiplayerLanSimulationHooks alwaysDeferredSimulationHooks(
    int& prepareCalls,
    int& acknowledgements
) {
    RuntimeMultiplayerLanSimulationHooks hooks;
    hooks.onStarted = [](
        const RuntimeMultiplayerSessionState&,
        std::string&
    ) {
        return true;
    };
    hooks.onCommandAdmitted = [](
        const RuntimeMultiplayerCommandPacket&,
        std::string&
    ) {
        return true;
    };
    hooks.prepareAuthoritativeAdvance = [&prepareCalls](
        int,
        std::string&
    ) {
        ++prepareCalls;
        return RuntimeMultiplayerLanSimulationAdvanceDecision::Deferred;
    };
    hooks.onAuthoritativeAdvance = [&acknowledgements](
        int,
        std::string&
    ) {
        ++acknowledgements;
        return true;
    };
    return hooks;
}

bool startDirectTwoPeerRoom(
    RuntimeMultiplayerLanSession& host,
    RuntimeMultiplayerLanSession& client,
    const std::string& roomId,
    std::uint64_t generation,
    int startTick,
    SmokeContext& context,
    std::uint32_t reconnectGraceMilliseconds = 0
) {
    RuntimeMultiplayerLanHostOptions hostOptions;
    hostOptions.session = sessionConfig();
    hostOptions.session.roomId = roomId;
    hostOptions.roomName = roomId;
    hostOptions.discoveryPort = 0;
    hostOptions.sessionPort = 0;
    hostOptions.roomGeneration = generation;
    hostOptions.reconnectGraceMilliseconds = reconnectGraceMilliseconds;
    std::string error;
    if (!host.startHost(hostOptions, error)) {
        context.expect(false, "direct fixture host should start: " + error);
        return false;
    }

    RuntimeLanRoomAdvertisement room;
    room.roomId = roomId;
    room.roomName = roomId;
    room.hostName = hostOptions.session.hostDisplayName;
    room.hostAddress = "127.0.0.1";
    room.contentFingerprint = "8877665544332211";
    room.protocolVersion = runtimeLanProtocolVersion;
    room.sessionPort = host.sessionPort();
    room.playerCount = 1;
    room.maximumPlayers = static_cast<std::uint8_t>(
        hostOptions.session.maximumMembers
    );
    RuntimeMultiplayerLanJoinOptions joinOptions;
    joinOptions.room = room;
    joinOptions.peerId = "client-peer";
    joinOptions.displayName = "Client Player";
    joinOptions.compatibility = hostOptions.session.compatibility;
    joinOptions.connectTimeoutMilliseconds = 2000;
    error.clear();
    if (!client.join(joinOptions, error)) {
        context.expect(false, "direct fixture client should join: " + error);
        return false;
    }
    if (!pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.phase() == RuntimeMultiplayerLanPhase::Lobby
                    && client.phase() == RuntimeMultiplayerLanPhase::Lobby
                    && host.session().members().size() == 2U
                    && client.session().members().size() == 2U;
            },
            context,
            "direct fixture admission"
        )) {
        return false;
    }

    const RuntimeMultiplayerMatchSetup setup = matchSetupFor(
        host.session(),
        1U,
        "maps/official/" + roomId + ".map"
    );
    const RuntimeMultiplayerLanOperationResult setupResult =
        host.setMatchSetup(setup);
    context.expect(
        setupResult.succeeded && setupResult.applied,
        "direct fixture setup should apply: " + setupResult.reason
    );
    if (!setupResult.succeeded
        || !pumpUntil(
            host,
            client,
            [&host, &client, &setup]() {
                return host.session().matchSetup().has_value()
                    && client.session().matchSetup().has_value()
                    && *host.session().matchSetup() == setup
                    && *client.session().matchSetup() == setup;
            },
            context,
            "direct fixture setup replication"
        )) {
        return false;
    }
    const RuntimeMultiplayerLanOperationResult hostReady = host.setReady(true);
    const RuntimeMultiplayerLanOperationResult clientReady = client.setReady(true);
    context.expect(
        hostReady.succeeded && clientReady.succeeded,
        "direct fixture members should submit ready"
    );
    if (!hostReady.succeeded || !clientReady.succeeded
        || !pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.session().allMembersReady()
                    && client.session().allMembersReady();
            },
            context,
            "direct fixture ready replication"
        )) {
        return false;
    }
    const RuntimeMultiplayerLanOperationResult started =
        host.startMatch(startTick);
    context.expect(
        started.succeeded && started.applied,
        "direct fixture should start: " + started.reason
    );
    return started.succeeded
        && pumpUntil(
            host,
            client,
            [&host, &client, startTick]() {
                return host.phase() == RuntimeMultiplayerLanPhase::Running
                    && client.phase() == RuntimeMultiplayerLanPhase::Running
                    && host.session().authoritativeTick() == startTick
                    && client.session().authoritativeTick() == startTick;
            },
            context,
            "direct fixture start replication"
        );
}

bool testReconnectTimeoutTerminates(SmokeContext& context) {
    RuntimeMultiplayerLanSession host;
    RuntimeMultiplayerLanSession client;
    if (!startDirectTwoPeerRoom(
            host,
            client,
            "lan-reconnect-timeout-room",
            0x445566778899aabbULL,
            25,
            context,
            100U
        )) {
        return false;
    }

    std::string error;
    const bool interrupted =
        client.interruptClientTransportForReconnect(error);
    context.expect(
        interrupted,
        "timeout fixture client should enter reconnect: " + error
    );
    if (!interrupted) return false;
    bool sawPending = false;
    bool sawFailed = false;
    bool sawAbort = false;
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline
        && host.phase() != RuntimeMultiplayerLanPhase::Aborted) {
        const RuntimeMultiplayerLanPollResult polled = host.poll(1);
        context.expect(
            polled.succeeded,
            "timeout fixture host poll failed: " + polled.error
        );
        if (!polled.succeeded) return false;
        for (const RuntimeMultiplayerLanEvent& event : polled.events) {
            sawPending = sawPending || event.kind
                == RuntimeMultiplayerLanEventKind::ReconnectPending;
            sawFailed = sawFailed || event.kind
                == RuntimeMultiplayerLanEventKind::ReconnectFailed;
            sawAbort = sawAbort || event.kind
                == RuntimeMultiplayerLanEventKind::SessionAborted;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const RuntimeMultiplayerLanPollResult clientPoll = client.poll(0);
    for (const RuntimeMultiplayerLanEvent& event : clientPoll.events) {
        sawFailed = sawFailed || event.kind
            == RuntimeMultiplayerLanEventKind::ReconnectFailed;
        sawAbort = sawAbort || event.kind
            == RuntimeMultiplayerLanEventKind::SessionAborted;
    }
    const bool terminated = clientPoll.succeeded
            && sawPending && sawFailed && sawAbort
            && host.phase() == RuntimeMultiplayerLanPhase::Aborted
            && client.phase() == RuntimeMultiplayerLanPhase::Aborted
            && host.session().abortState().affectedPeerId == "client-peer"
            && host.session().abortState().reason
                == RuntimeMultiplayerSessionAbortReason::TransportFailure;
    context.expect(
        terminated,
        "expired reconnect must explicitly abort both peers"
    );
    host.close();
    client.close();
    return terminated;
}

bool pumpThreeUntil(
    RuntimeMultiplayerLanSession& host,
    RuntimeMultiplayerLanSession& combatant,
    RuntimeMultiplayerLanSession& spectator,
    const std::function<bool()>& complete,
    SmokeContext& context,
    const std::string& description,
    int timeoutMilliseconds = 5000
) {
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMilliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const RuntimeMultiplayerLanPollResult hostPoll = host.poll(0);
        const RuntimeMultiplayerLanPollResult combatantPoll =
            combatant.poll(0);
        const RuntimeMultiplayerLanPollResult spectatorPoll =
            spectator.poll(0);
        if (!hostPoll.succeeded || !combatantPoll.succeeded
            || !spectatorPoll.succeeded) {
            context.expect(
                false,
                description + ": a three-peer poll failed"
            );
            return false;
        }
        if (complete()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(false, description + ": timed out");
    return false;
}

bool testRunningSpectatorLifecycle(SmokeContext& context) {
    RuntimeMultiplayerLanSession host;
    RuntimeMultiplayerLanSession combatant;
    RuntimeMultiplayerLanSession spectator;
    SnapshotHookProbe hostSnapshots;
    SnapshotHookProbe combatantSnapshots;
    SnapshotHookProbe spectatorSnapshots;
    std::string error;
    context.expect(
        host.setSnapshotHooks(snapshotHooks(hostSnapshots), error),
        "spectator fixture host snapshot hooks configure: " + error
    );
    error.clear();
    context.expect(
        combatant.setSnapshotHooks(
            snapshotHooks(combatantSnapshots),
            error
        ),
        "spectator fixture combatant snapshot hooks configure: " + error
    );
    error.clear();
    context.expect(
        spectator.setSnapshotHooks(
            snapshotHooks(spectatorSnapshots),
            error
        ),
        "spectator fixture observer snapshot hooks configure: " + error
    );

    RuntimeMultiplayerLanHostOptions hostOptions;
    hostOptions.session = sessionConfig();
    hostOptions.session.roomId = "lan-running-spectator-room";
    hostOptions.roomName = "Running Spectator Room";
    hostOptions.discoveryPort = 0;
    hostOptions.sessionPort = 0;
    hostOptions.roomGeneration = 0x66778899aabbccddULL;
    hostOptions.reconnectGraceMilliseconds = 0;
    error.clear();
    if (!host.startHost(hostOptions, error)) {
        context.expect(
            false,
            "spectator fixture host starts: " + error
        );
        return false;
    }

    RuntimeLanRoomAdvertisement room;
    room.roomId = hostOptions.session.roomId;
    room.roomName = hostOptions.roomName;
    room.hostName = hostOptions.session.hostDisplayName;
    room.hostAddress = "127.0.0.1";
    room.contentFingerprint = "8877665544332211";
    room.protocolVersion = runtimeLanProtocolVersion;
    room.sessionPort = host.sessionPort();
    room.playerCount = 1;
    room.maximumPlayers = static_cast<std::uint8_t>(
        hostOptions.session.maximumMembers
    );

    RuntimeMultiplayerLanJoinOptions fighterJoin;
    fighterJoin.room = room;
    fighterJoin.peerId = "fighter-peer";
    fighterJoin.displayName = "Fighter Player";
    fighterJoin.compatibility = hostOptions.session.compatibility;
    fighterJoin.connectTimeoutMilliseconds = 2000;
    RuntimeMultiplayerLanJoinOptions observerJoin = fighterJoin;
    observerJoin.peerId = "observer-peer";
    observerJoin.displayName = "Observer Player";
    observerJoin.participation =
        RuntimeMultiplayerParticipationRole::Spectator;
    error.clear();
    context.expect(
        combatant.join(fighterJoin, error),
        "spectator fixture combatant connects: " + error
    );
    error.clear();
    context.expect(
        spectator.join(observerJoin, error),
        "spectator fixture observer connects: " + error
    );
    if (!pumpThreeUntil(
            host,
            combatant,
            spectator,
            [&host, &combatant, &spectator]() {
                return host.phase() == RuntimeMultiplayerLanPhase::Lobby
                    && combatant.phase()
                        == RuntimeMultiplayerLanPhase::Lobby
                    && spectator.phase()
                        == RuntimeMultiplayerLanPhase::Lobby
                    && host.session().members().size() == 3U
                    && combatant.session().members().size() == 3U
                    && spectator.session().members().size() == 3U
                    && host.session().stateHash()
                        == combatant.session().stateHash()
                    && host.session().stateHash()
                        == spectator.session().stateHash();
            },
            context,
            "three-peer spectator admission"
        )) {
        return false;
    }

    const RuntimeMultiplayerMatchSetup setup = matchSetupFor(
        host.session(),
        1U,
        "maps/official/lan-running-spectator.map"
    );
    const RuntimeMultiplayerLanOperationResult setupResult =
        host.setMatchSetup(setup);
    context.expect(
        setupResult.succeeded && setupResult.applied,
        "spectator fixture setup applies: " + setupResult.reason
    );
    if (!pumpThreeUntil(
            host,
            combatant,
            spectator,
            [&host, &combatant, &spectator, &setup]() {
                return host.session().matchSetup().has_value()
                    && combatant.session().matchSetup().has_value()
                    && spectator.session().matchSetup().has_value()
                    && *host.session().matchSetup() == setup
                    && *combatant.session().matchSetup() == setup
                    && *spectator.session().matchSetup() == setup;
            },
            context,
            "three-peer spectator setup replication"
        )) {
        return false;
    }
    context.expect(
        host.setReady(true).succeeded
            && combatant.setReady(true).succeeded
            && spectator.setReady(true).succeeded,
        "three-peer spectator roster submits ready"
    );
    if (!pumpThreeUntil(
            host,
            combatant,
            spectator,
            [&host, &combatant, &spectator]() {
                return host.session().allMembersReady()
                    && combatant.session().allMembersReady()
                    && spectator.session().allMembersReady();
            },
            context,
            "three-peer spectator ready replication"
        )) {
        return false;
    }
    const RuntimeMultiplayerLanOperationResult started = host.startMatch(10);
    context.expect(
        started.succeeded && started.applied,
        "three-peer spectator match starts: " + started.reason
    );
    if (!pumpThreeUntil(
            host,
            combatant,
            spectator,
            [&host, &combatant, &spectator]() {
                return host.phase() == RuntimeMultiplayerLanPhase::Running
                    && combatant.phase()
                        == RuntimeMultiplayerLanPhase::Running
                    && spectator.phase()
                        == RuntimeMultiplayerLanPhase::Running;
            },
            context,
            "three-peer spectator start replication"
        )) {
        return false;
    }

    const RuntimeMultiplayerLanOperationResult observerLeft =
        spectator.leave();
    context.expect(
        observerLeft.succeeded && observerLeft.queued,
        "running spectator clean leave queues: " + observerLeft.reason
    );
    if (!pumpThreeUntil(
            host,
            combatant,
            spectator,
            [&host, &combatant, &spectator]() {
                return host.phase() == RuntimeMultiplayerLanPhase::Running
                    && combatant.phase()
                        == RuntimeMultiplayerLanPhase::Running
                    && spectator.phase()
                        == RuntimeMultiplayerLanPhase::Disconnected
                    && host.session().members().size() == 2U
                    && combatant.session().members().size() == 2U
                    && host.session().stateHash()
                        == combatant.session().stateHash();
            },
            context,
            "canonical running spectator leave"
        )) {
        return false;
    }

    context.expect(
        host.advanceAuthoritativeTick(11).succeeded,
        "combatants continue authoritative advancement after spectator leave"
    );
    if (!pumpThreeUntil(
            host,
            combatant,
            spectator,
            [&host, &combatant]() {
                return host.session().authoritativeTick() == 11
                    && combatant.session().authoritativeTick() == 11;
            },
            context,
            "post-spectator authoritative advancement"
        )) {
        return false;
    }
    context.expect(
        host.submitStateHash(10, 0xabcddcbaU).succeeded
            && combatant.submitStateHash(10, 0xabcddcbaU).succeeded,
        "remaining combatants submit post-leave state hashes"
    );
    if (!pumpThreeUntil(
            host,
            combatant,
            spectator,
            [&host, &combatant]() {
                return !host.session().stateHashFrames().empty()
                    && !combatant.session().stateHashFrames().empty()
                    && host.session().stateHashFrames().back().complete
                    && combatant.session().stateHashFrames().back().complete
                    && !host.session().stateHashFrames().back().desync
                    && !combatant.session().stateHashFrames().back().desync
                    && host.session().stateHash()
                        == combatant.session().stateHash();
            },
            context,
            "post-spectator state-hash consensus"
        )) {
        return false;
    }

    const std::vector<std::uint8_t> snapshotBytes(4097U, 0x5aU);
    const RuntimeMultiplayerSaveLoadProposal save = snapshotProposal(
        host.session(),
        1U,
        RuntimeMultiplayerSaveLoadKind::Save,
        10,
        "post-spectator-save",
        snapshotBytes,
        context
    );
    const RuntimeMultiplayerLanOperationResult proposed =
        host.proposeSaveLoad(save, snapshotBytes);
    context.expect(
        proposed.succeeded && proposed.applied,
        "remaining combatants coordinate save: " + proposed.reason
    );
    if (!pumpThreeUntil(
            host,
            combatant,
            spectator,
            [&host, &combatant]() {
                return !host.session().saveLoadHandshakes().empty()
                    && !combatant.session().saveLoadHandshakes().empty()
                    && host.session().saveLoadHandshakes().back().status
                        == RuntimeMultiplayerSaveLoadStatus::Committed
                    && combatant.session().saveLoadHandshakes().back().status
                        == RuntimeMultiplayerSaveLoadStatus::Committed
                    && host.session().saveLoadHandshakes().back()
                        .participants
                        == std::vector<std::string>{
                            "fighter-peer",
                            "host-peer"
                        }
                    && host.session().stateHash()
                        == combatant.session().stateHash();
            },
            context,
            "post-spectator coordinated save",
            10000
        )) {
        return false;
    }

    combatant.close();
    const auto abortDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < abortDeadline
        && host.phase() != RuntimeMultiplayerLanPhase::Aborted) {
        const RuntimeMultiplayerLanPollResult hostPoll = host.poll(1);
        context.expect(
            hostPoll.succeeded,
            "host detects combatant disconnect: " + hostPoll.error
        );
    }
    context.expect(
        host.phase() == RuntimeMultiplayerLanPhase::Aborted
            && host.session().status()
                == RuntimeMultiplayerRoomStatus::Aborted
            && host.session().abortState().affectedPeerId
                == "fighter-peer"
            && host.session().abortState().reason
                == RuntimeMultiplayerSessionAbortReason::PeerDisconnected,
        "combatant disconnect remains a deterministic match abort"
    );
    host.close();
    spectator.close();
    return true;
}

bool testPasswordProtectedAdmission(SmokeContext& context) {
    std::string error;
    RuntimeMultiplayerLanSession invalidHost;
    RuntimeMultiplayerLanHostOptions invalidOptions;
    invalidOptions.session = sessionConfig();
    invalidOptions.session.roomId = "lan-password-missing";
    invalidOptions.roomName = "Missing Password";
    invalidOptions.passwordProtected = true;
    invalidOptions.discoveryPort = 0U;
    invalidOptions.sessionPort = 0U;
    context.expect(
        !invalidHost.startHost(invalidOptions, error)
            && error.find("require") != std::string::npos,
        "LAN host must reject a protected flag without password material"
    );

    RuntimeMultiplayerLanSession host;
    RuntimeMultiplayerLanHostOptions hostOptions;
    hostOptions.session = sessionConfig();
    hostOptions.session.roomId = "lan-password-room";
    hostOptions.roomName = "Password Room";
    hostOptions.passwordProtected = true;
    hostOptions.password = "correct horse battery staple";
    hostOptions.discoveryPort = 0U;
    hostOptions.sessionPort = 0U;
    hostOptions.roomGeneration = 0x8f7e6d5c4b3a2910ULL;
    error.clear();
    if (!host.startHost(hostOptions, error)) {
        context.expect(false, "password LAN host should start: " + error);
        return false;
    }

    RuntimeMultiplayerLanSession browser;
    if (!browser.startBrowsing(host.discoveryPort(), error)
        || !browser.sendDiscoveryProbe("127.0.0.1", error)
        || !pumpUntil(
            host,
            browser,
            [&browser]() { return !browser.discoveredRooms().empty(); },
            context,
            "password LAN discovery"
        )) {
        context.expect(false, "password LAN room should be discoverable: " + error);
        host.close();
        browser.close();
        return false;
    }
    RuntimeLanRoomAdvertisement room = browser.discoveredRooms().front();
    context.expect(
        room.passwordProtected,
        "LAN discovery must advertise the password-protected bit"
    );
    browser.close();

    const auto joinOptions = [&](std::string peerId, std::string password) {
        RuntimeMultiplayerLanJoinOptions options;
        options.room = room;
        options.peerId = std::move(peerId);
        options.displayName = options.peerId;
        options.compatibility = hostOptions.session.compatibility;
        options.connectTimeoutMilliseconds = 2000;
        options.password = std::move(password);
        return options;
    };

    RuntimeMultiplayerLanSession emptyClient;
    error.clear();
    context.expect(
        !emptyClient.join(joinOptions("empty-password-peer", ""), error)
            && error.find("password") != std::string::npos,
        "protected LAN join must reject an empty password before connecting"
    );

    RuntimeMultiplayerLanSession wrongClient;
    error.clear();
    if (!wrongClient.join(
            joinOptions("wrong-password-peer", "wrong password"),
            error
        )) {
        context.expect(false, "wrong-password client should reach challenge: " + error);
        host.close();
        return false;
    }
    if (!pumpUntil(
            host,
            wrongClient,
            [&wrongClient]() {
                return wrongClient.phase()
                    == RuntimeMultiplayerLanPhase::JoinRejected;
            },
            context,
            "wrong LAN password rejection"
        )) {
        host.close();
        wrongClient.close();
        return false;
    }
    context.expect(
        host.session().members().size() == 1U,
        "wrong LAN password must not mutate the canonical roster"
    );
    wrongClient.close();
    (void)host.poll(0);

    RuntimeMultiplayerLanSession correctClient;
    error.clear();
    if (!correctClient.join(
            joinOptions(
                "correct-password-peer",
                "correct horse battery staple"
            ),
            error
        )) {
        context.expect(false, "correct-password client should connect: " + error);
        host.close();
        return false;
    }
    const bool admitted = pumpUntil(
        host,
        correctClient,
        [&host, &correctClient]() {
            return correctClient.phase() == RuntimeMultiplayerLanPhase::Lobby
                && host.session().members().size() == 2U
                && correctClient.session().members().size() == 2U;
        },
        context,
        "correct LAN password admission"
    );
    correctClient.close();
    host.close();
    return admitted;
}

} // namespace

int main() {
    SmokeContext context;
    RuntimeMultiplayerLanSession host;
    RuntimeMultiplayerLanSession client;
    RuntimeMultiplayerLockstepDriver hostDriver;
    RuntimeMultiplayerLockstepDriver clientDriver;
    SimulationHookProbe hostSimulation;
    SimulationHookProbe clientSimulation;
    SnapshotHookProbe hostSnapshots;
    SnapshotHookProbe clientSnapshots;
    clientSimulation.deferredAdvanceAttempts = 1;
    std::string error;

    RuntimeMultiplayerLanSnapshotHooks incompleteSnapshotHooks;
    incompleteSnapshotHooks.discardStaged = [](
        const RuntimeMultiplayerSnapshotDescriptor&
    ) {};
    context.expect(
        !host.setSnapshotHooks(
            std::move(incompleteSnapshotHooks),
            error
        ),
        "partial snapshot hook sets must be rejected"
    );

    context.expect(
        host.setCommandPayloadValidator(
            gameplayFrameValidator(),
            error
        ),
        "host gameplay-frame validator should configure: " + error
    );
    error.clear();
    context.expect(
        client.setCommandPayloadValidator(
            gameplayFrameValidator(),
            error
        ),
        "client gameplay-frame validator should configure: " + error
    );
    error.clear();
    context.expect(
        host.setSimulationHooks(
            lockstepSimulationHooks(hostDriver, hostSimulation),
            error
        ),
        "host simulation hooks should configure: " + error
    );
    error.clear();
    context.expect(
        client.setSimulationHooks(
            lockstepSimulationHooks(clientDriver, clientSimulation),
            error
        ),
        "client simulation hooks should configure: " + error
    );
    error.clear();
    context.expect(
        host.setSnapshotHooks(snapshotHooks(hostSnapshots), error),
        "host snapshot hooks should configure: " + error
    );
    error.clear();
    context.expect(
        client.setSnapshotHooks(snapshotHooks(clientSnapshots), error),
        "client snapshot hooks should configure: " + error
    );

    RuntimeMultiplayerLanHostOptions hostOptions;
    hostOptions.session = sessionConfig();
    hostOptions.roomName = "Loopback Room";
    hostOptions.discoveryPort = 0;
    hostOptions.sessionPort = 0;
    hostOptions.roomGeneration = 0x123456789abcdef0ULL;
    hostOptions.reconnectGraceMilliseconds = 0;
    context.expect(
        host.startHost(hostOptions, error),
        "host should start on ephemeral ports: " + error
    );
    error.clear();
    context.expect(
        !host.setSnapshotHooks({}, error),
        "snapshot hooks cannot change after hosting starts"
    );
    context.expect(host.discoveryPort() != 0, "host should publish discovery port");
    context.expect(host.sessionPort() != 0, "host should publish TCP port");
    context.expect(
        host.roomGeneration() == hostOptions.roomGeneration,
        "host should retain the configured room generation"
    );

    error.clear();
    context.expect(
        client.startBrowsing(host.discoveryPort(), error),
        "browser should open: " + error
    );
    error.clear();
    context.expect(
        client.sendDiscoveryProbe("127.0.0.1", error),
        "loopback discovery probe should send: " + error
    );

    const bool discovered = pumpUntil(
        host,
        client,
        [&client]() { return !client.discoveredRooms().empty(); },
        context,
        "LAN discovery"
    );
    if (!discovered) return 1;

    const std::vector<RuntimeLanRoomAdvertisement> rooms =
        client.discoveredRooms();
    context.expect(rooms.size() == 1U, "browser should discover exactly one room");
    const RuntimeLanRoomAdvertisement room = rooms.front();
    context.expect(room.hostAddress == "127.0.0.1", "browser should derive loopback host address");
    context.expect(room.sessionPort == host.sessionPort(), "advertisement should carry bound TCP port");
    context.expect(room.playerCount == 1U, "new room should advertise one member");

    RuntimeMultiplayerLanJoinOptions joinOptions;
    joinOptions.room = room;
    joinOptions.peerId = "client-peer";
    joinOptions.displayName = "Client Player";
    joinOptions.compatibility = hostOptions.session.compatibility;
    joinOptions.connectTimeoutMilliseconds = 2000;
    error.clear();
    context.expect(
        client.join(joinOptions, error),
        "client should connect to discovered room: " + error
    );

    const bool admitted = pumpUntil(
        host,
        client,
        [&host, &client]() {
            return host.phase() == RuntimeMultiplayerLanPhase::Lobby
                && client.phase() == RuntimeMultiplayerLanPhase::Lobby
                && host.session().members().size() == 2U
                && client.session().members().size() == 2U
                && host.session().stateHash() == client.session().stateHash();
        },
        context,
        "challenge/admission/lobby replication"
    );
    if (!admitted) return 1;

    context.expect(
        host.session().members()[0].peerId == "host-peer"
            && host.session().members()[1].peerId == "client-peer",
        "lobby roster should use canonical host/client order"
    );

    const RuntimeMultiplayerMatchSetup setup = matchSetupFor(
        host.session(),
        1U,
        "maps/official/lan-flow.map"
    );
    const RuntimeMultiplayerLanOperationResult setupResult =
        host.setMatchSetup(setup);
    context.expect(
        setupResult.succeeded && setupResult.applied,
        "host match setup should apply immediately: " + setupResult.reason
    );
    if (!pumpUntil(
            host,
            client,
            [&host, &client, &setup]() {
                return host.session().matchSetup().has_value()
                    && client.session().matchSetup().has_value()
                    && *host.session().matchSetup() == setup
                    && *client.session().matchSetup() == setup
                    && host.session().stateHash()
                        == client.session().stateHash();
            },
            context,
            "authoritative match-setup replication"
        )) {
        return 1;
    }

    const RuntimeMultiplayerLanOperationResult hostReady = host.setReady(true);
    const RuntimeMultiplayerLanOperationResult clientReady = client.setReady(true);
    context.expect(
        hostReady.succeeded && hostReady.applied,
        "host ready should apply immediately: " + hostReady.reason
    );
    context.expect(
        clientReady.succeeded && clientReady.queued,
        "client ready should queue for host authority: " + clientReady.reason
    );
    const bool ready = pumpUntil(
        host,
        client,
        [&host, &client]() {
            return host.session().allMembersReady()
                && client.session().allMembersReady()
                && host.session().stateHash() == client.session().stateHash();
        },
        context,
        "ready-state replication"
    );
    if (!ready) return 1;

    const std::vector<std::uint8_t> hostPayload =
        encodedGameplayFrame(
            RuntimeMultiplayerGameplayFrame{{stopCommand({11, 7, 11})}},
            context
        );
    const std::vector<std::uint8_t> clientPayload =
        encodedGameplayFrame(RuntimeMultiplayerGameplayFrame{}, context);
    const RuntimeMultiplayerLanOperationResult started = host.startMatch(12);
    context.expect(
        started.succeeded && started.applied,
        "host should start a ready room: " + started.reason
    );
    const RuntimeMultiplayerLanOperationResult hostCommand =
        host.submitCommand(12, 1, hostPayload);
    context.expect(
        hostCommand.succeeded && hostCommand.applied,
        "host command should follow Start before the client polls: "
            + hostCommand.reason
    );

    // Flush both queued canonical packets before the client's first read. The
    // LAN coordinator must synchronously run Start then Command hooks inside
    // this one poll, leaving no external driver-binding window.
    for (int flush = 0; flush < 8; ++flush) {
        const RuntimeMultiplayerLanPollResult hostPoll = host.poll(1);
        context.expect(
            hostPoll.succeeded,
            "host should flush Start/Command packets: " + hostPoll.error
        );
    }
    const RuntimeMultiplayerLanPollResult startAndCommandPoll = client.poll(1000);
    const auto startedEvent = std::find_if(
        startAndCommandPoll.events.begin(),
        startAndCommandPoll.events.end(),
        [](const RuntimeMultiplayerLanEvent& event) {
            return event.kind == RuntimeMultiplayerLanEventKind::Started;
        }
    );
    const auto commandEvent = std::find_if(
        startAndCommandPoll.events.begin(),
        startAndCommandPoll.events.end(),
        [](const RuntimeMultiplayerLanEvent& event) {
            return event.kind
                == RuntimeMultiplayerLanEventKind::CommandAccepted;
        }
    );
    const bool startAndCommandObserved =
        startAndCommandPoll.succeeded
        && !startAndCommandPoll.simulationDeferred
        && startedEvent != startAndCommandPoll.events.end()
        && commandEvent != startAndCommandPoll.events.end()
        && startedEvent < commandEvent
        && client.phase() == RuntimeMultiplayerLanPhase::Running
        && client.session().commands().size() == 1U
        && clientSimulation.order
            == std::vector<std::string>{
                "start",
                "command:host-peer:1"
            };
    context.expect(
        startAndCommandObserved,
        "one client poll should admit Start then Command and run hooks in order"
    );
    if (!startAndCommandObserved) return 1;
    context.expect(
        hostDriver.bound() && clientDriver.bound()
            && hostSimulation.starts == 1
            && clientSimulation.starts == 1
            && host.session().stateHash() == client.session().stateHash(),
        "Start hooks bind const host/client drivers before commands"
    );

    const std::uint64_t hostBeforeLocalMalformed =
        host.session().stateHash();
    const std::uint64_t clientBeforeLocalMalformed =
        client.session().stateHash();
    const RuntimeMultiplayerLanOperationResult rejectedHostPayload =
        host.submitCommand(12, 1, {0x43U, 0x47U});
    const RuntimeMultiplayerLanOperationResult rejectedClientPayload =
        client.submitCommand(12, 1, {0x43U, 0x47U});
    context.expect(
        !rejectedHostPayload.succeeded
            && !rejectedHostPayload.applied
            && !rejectedClientPayload.succeeded
            && !rejectedClientPayload.queued
            && host.session().commands().size() == 1U
            && client.session().commands().size() == 1U
            && host.session().stateHash() == hostBeforeLocalMalformed
            && client.session().stateHash() == clientBeforeLocalMalformed,
        "local malformed frames are rejected before Session or transport"
    );

    const RuntimeMultiplayerLanOperationResult missingFrameAdvance =
        host.advanceAuthoritativeTick(13);
    context.expect(
        missingFrameAdvance.succeeded
            && missingFrameAdvance.deferred
            && !missingFrameAdvance.applied
            && host.phase() == RuntimeMultiplayerLanPhase::Running
            && host.session().authoritativeTick() == 12
            && !hostDriver.tickCompleted(12),
        "a missing combatant frame should defer host advance without error"
    );

    const RuntimeMultiplayerLanOperationResult clientCommand =
        client.submitCommand(12, 1, clientPayload);
    context.expect(
        clientCommand.succeeded && clientCommand.queued,
        "client command should queue: " + clientCommand.reason
    );
    const RuntimeMultiplayerLanPollResult clientSendPoll = client.poll(0);
    context.expect(
        clientSendPoll.succeeded && !clientSendPoll.simulationDeferred,
        "client command transport should flush: " + clientSendPoll.error
    );
    const auto hostCommandDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < hostCommandDeadline
        && host.session().commands().size() != 2U) {
        const RuntimeMultiplayerLanPollResult hostPoll = host.poll(1);
        if (!hostPoll.succeeded) {
            context.expect(
                false,
                "host command ingress failed: " + hostPoll.error
            );
            break;
        }
    }
    context.expect(
        host.session().commands().size() == 2U,
        "host should admit the remote frame before advancing"
    );
    if (host.session().commands().size() != 2U) return 1;
    context.expect(
        host.session().commands()[0].peerId == "client-peer"
            && host.session().commands()[1].peerId == "host-peer",
        "commands should be ordered by tick, peer and sequence"
    );
    std::vector<RuntimeMultiplayerLockstepApplyCommand> hostApply;
    error.clear();
    const RuntimeMultiplayerOperationResult idempotentObservation =
        hostDriver.observeAdmittedFrame(host.session().commands().front());
    context.expect(
        idempotentObservation.accepted
            && idempotentObservation.duplicate
            && hostDriver.inputHorizonReady()
            && hostDriver.canonicalApplyList(12, hostApply, error)
            && hostApply.size() == 1U
            && hostApply.front().peerId == "host-peer"
            && hostSimulation.commands == 2,
        "host hooks should atomically observe empty/non-empty frames: "
            + error
    );

    hostSimulation.failNextApply = true;
    const RuntimeMultiplayerLanOperationResult failedTick =
        host.advanceAuthoritativeTick(13);
    context.expect(
        !failedTick.succeeded
            && !failedTick.applied
            && host.phase() == RuntimeMultiplayerLanPhase::Running
            && host.session().authoritativeTick() == 12
            && !hostDriver.tickCompleted(12)
            && hostSimulation.applyCalls == 1
            && hostSimulation.acknowledgements == 0,
        "world apply failure should leave both driver and Session unadvanced"
    );

    const RuntimeMultiplayerLanOperationResult tick =
        host.advanceAuthoritativeTick(13);
    context.expect(
        tick.succeeded && tick.applied
            && host.session().authoritativeTick() == 13
            && hostDriver.currentBarrierTick() == 13
            && hostSimulation.applyCalls == 2
            && hostSimulation.acknowledgements == 1,
        "retry should apply, advance Session, then acknowledge: " + tick.reason
    );
    const std::vector<std::uint8_t> nextHostPayload =
        encodedGameplayFrame(RuntimeMultiplayerGameplayFrame{}, context);
    const RuntimeMultiplayerLanOperationResult nextHostCommand =
        host.submitCommand(13, 2, nextHostPayload);
    context.expect(
        nextHostCommand.succeeded && nextHostCommand.applied,
        "a later canonical command should queue behind the client advance"
    );
    for (int flush = 0; flush < 8; ++flush) {
        const RuntimeMultiplayerLanPollResult hostPoll = host.poll(1);
        context.expect(
            hostPoll.succeeded,
            "host should flush command/advance/next-command packets: "
                + hostPoll.error
        );
    }

    const RuntimeMultiplayerLanPollResult deferredPoll = client.poll(1000);
    context.expect(
        deferredPoll.succeeded
            && deferredPoll.simulationDeferred
            && client.phase() == RuntimeMultiplayerLanPhase::Running
            && client.session().authoritativeTick() == 12
            && client.session().commands().size() == 2U
            && clientDriver.tickCompleted(12)
            && clientSimulation.applyCalls == 1
            && clientSimulation.acknowledgements == 0
            && clientSimulation.commands == 2,
        "client should admit the echo, apply once, retain TickAdvance, and stop"
    );
    const RuntimeMultiplayerLanPollResult retryPoll = client.poll(1000);
    context.expect(
        retryPoll.succeeded
            && !retryPoll.simulationDeferred
            && client.session().authoritativeTick() == 13
            && client.session().commands().size() == 1U
            && client.session().commands().front().tick == 13
            && clientDriver.currentBarrierTick() == 13
            && clientSimulation.applyCalls == 1
            && clientSimulation.acknowledgements == 1
            && clientSimulation.commands == 3
            && host.session().stateHash() == client.session().stateHash()
            && hostDriver.stateHash() == clientDriver.stateHash(),
        "next poll should retry without reapply, then ack and release later packets"
    );
    const std::vector<std::string> expectedClientOrder{
        "start",
        "command:host-peer:1",
        "command:client-peer:1",
        "prepare:12",
        "prepare:12",
        "ack:12",
        "command:host-peer:2"
    };
    context.expect(
        clientSimulation.order == expectedClientOrder,
        "client hook order should be command, prepare/apply, Session advance, ack"
    );

    const int hostApplyCallsBeforeDuplicate = hostSimulation.applyCalls;
    const int clientApplyCallsBeforeDuplicate = clientSimulation.applyCalls;
    const RuntimeMultiplayerLanOperationResult duplicateTick =
        host.advanceAuthoritativeTick(13);
    context.expect(
        duplicateTick.succeeded && duplicateTick.applied
            && duplicateTick.duplicate
            && hostSimulation.applyCalls == hostApplyCallsBeforeDuplicate,
        "duplicate authoritative advance should be idempotent and never reapply"
    );
    if (!pumpUntil(
            host,
            client,
            [&host, &client, &hostSimulation, &clientSimulation,
             clientApplyCallsBeforeDuplicate]() {
                return host.session().stateHash() == client.session().stateHash()
                    && hostSimulation.acknowledgements == 2
                    && clientSimulation.acknowledgements == 2
                    && clientSimulation.applyCalls
                        == clientApplyCallsBeforeDuplicate;
            },
            context,
            "idempotent canonical advance acknowledgement"
        )) {
        return 1;
    }
    context.expect(
        hostDriver.currentBarrierTick() == 13
            && clientDriver.currentBarrierTick() == 13,
        "duplicate advance acknowledgement must not move either barrier"
    );

    constexpr std::uint64_t worldHash = 0x13579bdf2468ace0ULL;
    const RuntimeMultiplayerLanOperationResult hostHash =
        host.submitStateHash(13, worldHash);
    const RuntimeMultiplayerLanOperationResult clientHash =
        client.submitStateHash(13, worldHash);
    context.expect(hostHash.succeeded, "host state hash should submit: " + hostHash.reason);
    context.expect(clientHash.succeeded, "client state hash should queue: " + clientHash.reason);
    if (!pumpUntil(
            host,
            client,
            [&host, &client]() {
                const auto& left = host.session().stateHashFrames();
                const auto& right = client.session().stateHashFrames();
                return left.size() == 1U
                    && right.size() == 1U
                    && left == right
                    && left.front().complete
                    && !left.front().desync
                    && host.session().stateHash() == client.session().stateHash();
            },
            context,
            "equal world-state hash reports"
        )) {
        return 1;
    }

    const RuntimeMultiplayerLanOperationResult nextClientCommand =
        client.submitCommand(13, 2, clientPayload);
    context.expect(
        nextClientCommand.succeeded && nextClientCommand.queued,
        "client should fill the next input barrier before save consensus"
    );
    if (!pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.session().commands().size() == 2U
                    && client.session().commands().size() == 2U
                    && host.session().commands()
                        == client.session().commands()
                    && host.session().stateHash()
                        == client.session().stateHash();
            },
            context,
            "next input barrier replication"
        )) {
        return 1;
    }
    context.expect(
        hostDriver.frameBarrierReady(13)
            && hostDriver.inputHorizonReady()
            && !hostDriver.tickCompleted(13),
        "save-barrier fixture should otherwise be simulation-ready"
    );

    std::vector<std::uint8_t> saveBytes(
        runtimeMultiplayerSnapshotChunkBytes + 17U
    );
    for (std::size_t index = 0U; index < saveBytes.size(); ++index) {
        saveBytes[index] = static_cast<std::uint8_t>(index * 29U + 7U);
    }
    RuntimeMultiplayerSaveLoadProposal proposal = snapshotProposal(
        host.session(),
        1,
        RuntimeMultiplayerSaveLoadKind::Save,
        host.session().authoritativeTick() - 1,
        "slot-1-generation-1",
        saveBytes,
        context
    );
    const RuntimeMultiplayerLanOperationResult proposed =
        host.proposeSaveLoad(proposal, saveBytes);
    context.expect(proposed.succeeded, "save consensus should be proposed: " + proposed.reason);
    if (!pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.session().saveLoadHandshakes().size() == 1U
                    && client.session().saveLoadHandshakes().size() == 1U
                    && host.session().stateHash() == client.session().stateHash();
            },
            context,
            "save proposal replication"
        )) {
        return 1;
    }
    const int prepareCallsBeforeSaveBarrier = hostSimulation.prepareCalls;
    const int applyCallsBeforeSaveBarrier = hostSimulation.applyCalls;
    const int acknowledgementsBeforeSaveBarrier =
        hostSimulation.acknowledgements;
    const int snapshotStagesBeforeConflict = hostSnapshots.stages;
    RuntimeMultiplayerSaveLoadProposal conflictingProposal = proposal;
    conflictingProposal.proposalId = 2;
    conflictingProposal.descriptor.identity.proposalId = 2;
    const RuntimeMultiplayerLanOperationResult conflictingProposalResult =
        host.proposeSaveLoad(conflictingProposal, saveBytes);
    context.expect(
        !conflictingProposalResult.succeeded
            && hostSnapshots.stages == snapshotStagesBeforeConflict
            && hostSnapshots.commits == 0,
        "active Session barrier must reject a second proposal before hooks"
    );
    const RuntimeMultiplayerLanOperationResult saveBlockedAdvance =
        host.advanceAuthoritativeTick(14);
    context.expect(
        !saveBlockedAdvance.succeeded
            && !saveBlockedAdvance.applied
            && !saveBlockedAdvance.deferred
            && host.phase() == RuntimeMultiplayerLanPhase::Running
            && host.session().authoritativeTick() == 13
            && !hostDriver.tickCompleted(13)
            && hostDriver.currentBarrierTick() == 13
            && hostSimulation.prepareCalls
                == prepareCallsBeforeSaveBarrier
            && hostSimulation.applyCalls == applyCallsBeforeSaveBarrier
            && hostSimulation.acknowledgements
                == acknowledgementsBeforeSaveBarrier
            && host.session().stateHash() == client.session().stateHash(),
        "Session dry-run must reject a save-barrier advance before world hooks"
    );
    if (!pumpUntil(
            host,
            client,
            [&host, &client]() {
                const auto& left = host.session().saveLoadHandshakes();
                const auto& right = client.session().saveLoadHandshakes();
                return left == right
                    && left.front().status
                        == RuntimeMultiplayerSaveLoadStatus::Committed
                    && host.session().stateHash() == client.session().stateHash();
            },
            context,
            "automatic snapshot transfer and save commit",
            15000
        )) {
        return 1;
    }
    context.expect(
        hostSnapshots.stages == 1 && clientSnapshots.stages == 1
            && hostSnapshots.commits == 1
            && clientSnapshots.commits == 1
            && hostSnapshots.postCommits == 0
            && clientSnapshots.postCommits == 0
            && hostSnapshots.stagedBytes == saveBytes
            && clientSnapshots.stagedBytes == saveBytes,
        "save should stage exact verified bytes and commit once without rebind"
    );

    client.close();
    const auto disconnectDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < disconnectDeadline
        && host.phase() != RuntimeMultiplayerLanPhase::Aborted) {
        const RuntimeMultiplayerLanPollResult poll = host.poll(1);
        if (!poll.succeeded) {
            context.expect(false, "host disconnect poll failed: " + poll.error);
            break;
        }
    }
    context.expect(
        host.phase() == RuntimeMultiplayerLanPhase::Aborted,
        "running client disconnect should abort the authoritative session"
    );
    context.expect(
        host.session().status() == RuntimeMultiplayerRoomStatus::Aborted
            && host.session().abortState().affectedPeerId == "client-peer"
            && host.session().abortState().tick == 13,
        "disconnect abort should identify the departed peer and fixed tick"
    );

    host.close();
    context.expect(
        host.phase() == RuntimeMultiplayerLanPhase::Inactive,
        "close should reset the host coordinator"
    );
    error.clear();
    context.expect(
        host.setSimulationHooks({}, error),
        "closed host should clear simulation hooks: " + error
    );
    error.clear();
    context.expect(
        client.setSimulationHooks({}, error),
        "closed client should clear simulation hooks: " + error
    );

    // A malicious host is still opaque locally, but a validating client must
    // reject its malformed canonical command before mutating replicated state.
    error.clear();
    context.expect(
        host.setCommandPayloadValidator({}, error),
        "closed host should restore opaque command transport: " + error
    );
    error.clear();
    context.expect(
        client.setCommandPayloadValidator(
            gameplayFrameValidator(),
            error
        ),
        "closed client should retain frame validation: " + error
    );
    RuntimeMultiplayerLanHostOptions badHostOptions;
    badHostOptions.session = sessionConfig();
    badHostOptions.session.roomId = "lan-bad-host-room";
    badHostOptions.roomName = "Bad Host Room";
    badHostOptions.discoveryPort = 0;
    badHostOptions.sessionPort = 0;
    badHostOptions.roomGeneration = 0x1122334455667788ULL;
    badHostOptions.reconnectGraceMilliseconds = 0;
    error.clear();
    context.expect(
        host.startHost(badHostOptions, error),
        "bad-host fixture should start: " + error
    );
    RuntimeLanRoomAdvertisement badHostRoom;
    badHostRoom.roomId = badHostOptions.session.roomId;
    badHostRoom.roomName = badHostOptions.roomName;
    badHostRoom.hostName = badHostOptions.session.hostDisplayName;
    badHostRoom.hostAddress = "127.0.0.1";
    badHostRoom.contentFingerprint = "8877665544332211";
    badHostRoom.protocolVersion = runtimeLanProtocolVersion;
    badHostRoom.sessionPort = host.sessionPort();
    badHostRoom.playerCount = 1;
    badHostRoom.maximumPlayers = static_cast<std::uint8_t>(
        badHostOptions.session.maximumMembers
    );
    RuntimeMultiplayerLanJoinOptions badHostJoin;
    badHostJoin.room = badHostRoom;
    badHostJoin.peerId = "client-peer";
    badHostJoin.displayName = "Client Player";
    badHostJoin.compatibility = badHostOptions.session.compatibility;
    badHostJoin.connectTimeoutMilliseconds = 2000;
    error.clear();
    context.expect(
        client.join(badHostJoin, error),
        "validating client should join bad-host fixture: " + error
    );
    if (!pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.phase() == RuntimeMultiplayerLanPhase::Lobby
                    && client.phase() == RuntimeMultiplayerLanPhase::Lobby
                    && host.session().members().size() == 2U
                    && host.session().stateHash()
                        == client.session().stateHash();
            },
            context,
            "bad-host fixture admission"
        )) {
        return 1;
    }
    const RuntimeMultiplayerMatchSetup badHostSetup = matchSetupFor(
        host.session(),
        1U,
        "maps/official/lan-bad-host.map"
    );
    context.expect(
        host.setMatchSetup(badHostSetup).succeeded,
        "bad-host fixture match setup should apply"
    );
    if (!pumpUntil(
            host,
            client,
            [&host, &client, &badHostSetup]() {
                return host.session().matchSetup().has_value()
                    && client.session().matchSetup().has_value()
                    && *host.session().matchSetup() == badHostSetup
                    && *client.session().matchSetup() == badHostSetup;
            },
            context,
            "bad-host fixture setup replication"
        )) {
        return 1;
    }
    context.expect(
        host.setReady(true).succeeded
            && client.setReady(true).succeeded,
        "bad-host fixture members should ready"
    );
    if (!pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.session().allMembersReady()
                    && client.session().allMembersReady();
            },
            context,
            "bad-host fixture ready replication"
        )) {
        return 1;
    }
    context.expect(
        host.startMatch(20).succeeded,
        "bad-host fixture should enter running"
    );
    if (!pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.phase() == RuntimeMultiplayerLanPhase::Running
                    && client.phase() == RuntimeMultiplayerLanPhase::Running;
            },
            context,
            "bad-host fixture start replication"
        )) {
        return 1;
    }
    context.expect(
        host.session().commands().empty()
            && client.session().commands().empty(),
        "bad-host fixture begins without commands"
    );
    const RuntimeMultiplayerLanOperationResult badCanonical =
        host.submitCommand(20, 1, {0x43U, 0x47U});
    context.expect(
        badCanonical.succeeded && badCanonical.applied,
        "opaque malicious host can author a malformed canonical payload"
    );
    if (!pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.phase() == RuntimeMultiplayerLanPhase::Aborted
                    && client.phase() == RuntimeMultiplayerLanPhase::Aborted;
            },
            context,
            "bad host canonical rejection"
        )) {
        return 1;
    }
    context.expect(
        client.session().commands().empty()
            && client.session().abortState().affectedPeerId == "host-peer"
            && client.session().abortState().reason
                == RuntimeMultiplayerSessionAbortReason::ProtocolViolation,
        "validating client rejects bad host payload before Session mutation"
    );
    host.close();
    client.close();

    // A second real socket room proves that spectator admission reaches the
    // coordinator, participates in ready/hash consensus, and cannot cross the
    // gameplay-command authority boundary.
    RuntimeMultiplayerLanSession spectatorHost;
    RuntimeMultiplayerLanSession fighter;
    RuntimeMultiplayerLanSession observer;
    SnapshotHookProbe spectatorHostSnapshots;
    SnapshotHookProbe fighterSnapshots;
    SnapshotHookProbe observerSnapshots;
    RuntimeMultiplayerLockstepDriver spectatorHostDriver;
    RuntimeMultiplayerLockstepDriver fighterDriver;
    RuntimeMultiplayerLockstepDriver observerDriver;
    spectatorHostSnapshots.lockstepDriver = &spectatorHostDriver;
    fighterSnapshots.lockstepDriver = &fighterDriver;
    observerSnapshots.lockstepDriver = &observerDriver;
    error.clear();
    context.expect(
        spectatorHost.setCommandPayloadValidator(
            gameplayFrameValidator(),
            error
        ),
        "spectator-room host validator should configure: " + error
    );
    error.clear();
    context.expect(
        fighter.setCommandPayloadValidator(
            gameplayFrameValidator(),
            error
        ),
        "spectator-room fighter validator should configure: " + error
    );
    error.clear();
    context.expect(
        spectatorHost.setSnapshotHooks(
            snapshotHooks(spectatorHostSnapshots),
            error
        ),
        "spectator host snapshot hooks should configure: " + error
    );
    error.clear();
    context.expect(
        fighter.setSnapshotHooks(snapshotHooks(fighterSnapshots), error),
        "fighter snapshot hooks should configure: " + error
    );
    error.clear();
    context.expect(
        observer.setSnapshotHooks(snapshotHooks(observerSnapshots), error),
        "spectator snapshot hooks should configure: " + error
    );
    RuntimeMultiplayerLanHostOptions spectatorHostOptions;
    spectatorHostOptions.session = sessionConfig();
    spectatorHostOptions.session.roomId = "lan-spectator-room";
    spectatorHostOptions.session.maximumMembers = 4;
    spectatorHostOptions.roomName = "Spectator Room";
    spectatorHostOptions.discoveryPort = 0;
    spectatorHostOptions.sessionPort = 0;
    spectatorHostOptions.roomGeneration = 0x2233445566778899ULL;
    error.clear();
    context.expect(
        spectatorHost.startHost(spectatorHostOptions, error),
        "spectator host should start: " + error
    );

    RuntimeLanRoomAdvertisement spectatorRoom;
    spectatorRoom.roomId = spectatorHostOptions.session.roomId;
    spectatorRoom.roomName = spectatorHostOptions.roomName;
    spectatorRoom.hostName = spectatorHostOptions.session.hostDisplayName;
    spectatorRoom.hostAddress = "127.0.0.1";
    spectatorRoom.contentFingerprint = "8877665544332211";
    spectatorRoom.protocolVersion = runtimeLanProtocolVersion;
    spectatorRoom.sessionPort = spectatorHost.sessionPort();
    spectatorRoom.playerCount = 1;
    spectatorRoom.maximumPlayers = 4;

    RuntimeMultiplayerLanJoinOptions fighterJoin;
    fighterJoin.room = spectatorRoom;
    fighterJoin.peerId = "fighter-peer";
    fighterJoin.displayName = "Fighter Player";
    fighterJoin.compatibility = spectatorHostOptions.session.compatibility;
    fighterJoin.connectTimeoutMilliseconds = 2000;
    error.clear();
    context.expect(
        fighter.join(fighterJoin, error),
        "fighter should connect: " + error
    );
    RuntimeMultiplayerLanJoinOptions observerJoin;
    observerJoin.room = spectatorRoom;
    observerJoin.peerId = "observer-peer";
    observerJoin.displayName = "Observer Player";
    observerJoin.participation =
        RuntimeMultiplayerParticipationRole::Spectator;
    observerJoin.compatibility = spectatorHostOptions.session.compatibility;
    observerJoin.connectTimeoutMilliseconds = 2000;
    error.clear();
    context.expect(
        observer.join(observerJoin, error),
        "spectator should connect: " + error
    );

    const auto pumpSpectatorRoom = [
        &spectatorHost,
        &fighter,
        &observer,
        &context
    ](
        const std::function<bool()>& complete,
        const std::string& description,
        int timeoutMilliseconds = 5000
    ) {
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(timeoutMilliseconds);
        while (std::chrono::steady_clock::now() < deadline) {
            const RuntimeMultiplayerLanPollResult hostPoll =
                spectatorHost.poll(0);
            const RuntimeMultiplayerLanPollResult fighterPoll = fighter.poll(0);
            const RuntimeMultiplayerLanPollResult observerPoll =
                observer.poll(0);
            if (!hostPoll.succeeded
                || !fighterPoll.succeeded
                || !observerPoll.succeeded) {
                context.expect(false, description + ": LAN poll failed");
                return false;
            }
            if (complete()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        context.expect(false, description + ": timed out");
        return false;
    };

    if (!pumpSpectatorRoom(
            [&spectatorHost, &fighter, &observer]() {
                const auto& members = spectatorHost.session().members();
                const auto spectator = std::find_if(
                    members.begin(),
                    members.end(),
                    [](const RuntimeMultiplayerRoomMember& member) {
                        return member.peerId == "observer-peer";
                    }
                );
                return spectatorHost.phase()
                        == RuntimeMultiplayerLanPhase::Lobby
                    && fighter.phase() == RuntimeMultiplayerLanPhase::Lobby
                    && observer.phase() == RuntimeMultiplayerLanPhase::Lobby
                    && members.size() == 3U
                    && spectator != members.end()
                    && spectator->participation
                        == RuntimeMultiplayerParticipationRole::Spectator
                    && spectatorHost.session().stateHash()
                        == fighter.session().stateHash()
                    && spectatorHost.session().stateHash()
                        == observer.session().stateHash();
            },
            "spectator admission and snapshot replication"
        )) {
        return 1;
    }

    const RuntimeMultiplayerMatchSetup spectatorSetup = matchSetupFor(
        spectatorHost.session(),
        1U,
        "maps/official/lan-spectator.map"
    );
    const RuntimeMultiplayerLanOperationResult spectatorSetupResult =
        spectatorHost.setMatchSetup(spectatorSetup);
    context.expect(
        spectatorSetupResult.succeeded && spectatorSetupResult.applied,
        "spectator-room match setup should apply: "
            + spectatorSetupResult.reason
    );
    if (!pumpSpectatorRoom(
            [&spectatorHost, &fighter, &observer, &spectatorSetup]() {
                return spectatorHost.session().matchSetup().has_value()
                    && fighter.session().matchSetup().has_value()
                    && observer.session().matchSetup().has_value()
                    && *spectatorHost.session().matchSetup()
                        == spectatorSetup
                    && *fighter.session().matchSetup() == spectatorSetup
                    && *observer.session().matchSetup() == spectatorSetup
                    && spectatorHost.session().stateHash()
                        == fighter.session().stateHash()
                    && spectatorHost.session().stateHash()
                        == observer.session().stateHash();
            },
            "spectator-room match-setup replication"
        )) {
        return 1;
    }

    context.expect(
        spectatorHost.setReady(true).succeeded
            && fighter.setReady(true).succeeded
            && observer.setReady(true).succeeded,
        "host, fighter and spectator should submit ready state"
    );
    if (!pumpSpectatorRoom(
            [&spectatorHost, &fighter, &observer]() {
                return spectatorHost.session().allMembersReady()
                    && fighter.session().allMembersReady()
                    && observer.session().allMembersReady()
                    && spectatorHost.session().stateHash()
                        == fighter.session().stateHash()
                    && spectatorHost.session().stateHash()
                        == observer.session().stateHash();
            },
            "spectator ready consensus"
        )) {
        return 1;
    }
    context.expect(
        spectatorHost.startMatch(30).succeeded,
        "spectator room should start"
    );
    if (!pumpSpectatorRoom(
            [&spectatorHost, &fighter, &observer]() {
                return spectatorHost.phase()
                        == RuntimeMultiplayerLanPhase::Running
                    && fighter.phase() == RuntimeMultiplayerLanPhase::Running
                    && observer.phase() == RuntimeMultiplayerLanPhase::Running
                    && spectatorHost.session().stateHash()
                        == fighter.session().stateHash()
                    && spectatorHost.session().stateHash()
                        == observer.session().stateHash();
            },
            "spectator start replication"
        )) {
        return 1;
    }

    context.expect(
        spectatorHostDriver.bind(spectatorHost.session()).accepted
            && fighterDriver.bind(fighter.session()).accepted
            && observerDriver.bind(observer.session()).accepted,
        "three-member lockstep observers should bind before snapshot transfer"
    );

    constexpr std::uint64_t spectatorWorldHash = 0x2468ace013579bdfULL;
    context.expect(
        spectatorHost.submitStateHash(30, spectatorWorldHash).succeeded
            && fighter.submitStateHash(30, spectatorWorldHash).succeeded
            && observer.submitStateHash(30, spectatorWorldHash).succeeded,
        "spectator should submit a world hash but no command"
    );
    if (!pumpSpectatorRoom(
            [&spectatorHost, &fighter, &observer]() {
                const auto& frames = spectatorHost.session().stateHashFrames();
                return frames.size() == 1U
                    && frames.front().complete
                    && !frames.front().desync
                    && spectatorHost.session().stateHash()
                        == fighter.session().stateHash()
                    && spectatorHost.session().stateHash()
                        == observer.session().stateHash();
            },
            "spectator world-hash consensus"
        )) {
        return 1;
    }

    std::vector<std::uint8_t> spectatorSaveBytes(
        runtimeMultiplayerSnapshotChunkBytes * 2U + 31U,
        0x5aU
    );
    RuntimeMultiplayerSaveLoadProposal spectatorSave = snapshotProposal(
        spectatorHost.session(),
        1,
        RuntimeMultiplayerSaveLoadKind::Save,
        spectatorHost.session().authoritativeTick() - 1,
        "spectator-save-generation-1",
        spectatorSaveBytes,
        context
    );
    const RuntimeMultiplayerLanOperationResult spectatorSaveResult =
        spectatorHost.proposeSaveLoad(
            spectatorSave,
            spectatorSaveBytes
        );
    context.expect(
        spectatorSaveResult.succeeded,
        "three-member save transfer should start: "
            + spectatorSaveResult.reason
    );
    if (!pumpSpectatorRoom(
            [&spectatorHost, &fighter, &observer]() {
                const auto& handshakes =
                    spectatorHost.session().saveLoadHandshakes();
                return !handshakes.empty()
                    && handshakes.back().status
                        == RuntimeMultiplayerSaveLoadStatus::Committed
                    && spectatorHost.session().stateHash()
                        == fighter.session().stateHash()
                    && spectatorHost.session().stateHash()
                        == observer.session().stateHash();
            },
            "three-member automatic save commit",
            30000
        )) {
        return 1;
    }
    context.expect(
        spectatorHostSnapshots.stages == 1
            && fighterSnapshots.stages == 1
            && observerSnapshots.stages == 1
            && spectatorHostSnapshots.commits == 1
            && fighterSnapshots.commits == 1
            && observerSnapshots.commits == 1
            && spectatorHostSnapshots.postCommits == 0
            && fighterSnapshots.postCommits == 0
            && observerSnapshots.postCommits == 0
            && spectatorHostDriver.timelineEpoch() == 1U
            && fighterDriver.timelineEpoch() == 1U
            && observerDriver.timelineEpoch() == 1U
            && spectatorHostDriver.currentBarrierTick() == 30
            && fighterDriver.currentBarrierTick() == 30
            && observerDriver.currentBarrierTick() == 30,
        "combatants and spectator should all stage/commit Save exactly once"
    );

    const std::uint64_t epochBeforeLoad =
        spectatorHost.session().timelineEpoch();
    std::vector<std::uint8_t> spectatorLoadBytes(
        5U * 1024U * 1024U + 97U
    );
    for (std::size_t index = 0U; index < spectatorLoadBytes.size(); ++index) {
        spectatorLoadBytes[index] = static_cast<std::uint8_t>(
            (index * 131U + index / 17U) & 0xffU
        );
    }
    constexpr int restoredSnapshotTick = 24;
    RuntimeMultiplayerSaveLoadProposal spectatorLoad = snapshotProposal(
        spectatorHost.session(),
        2,
        RuntimeMultiplayerSaveLoadKind::Load,
        restoredSnapshotTick,
        "spectator-load-generation-2",
        spectatorLoadBytes,
        context
    );
    const RuntimeMultiplayerLanOperationResult spectatorLoadResult =
        spectatorHost.proposeSaveLoad(
            spectatorLoad,
            spectatorLoadBytes
        );
    context.expect(
        spectatorLoadResult.succeeded,
        ">5 MiB three-member load transfer should start: "
            + spectatorLoadResult.reason
    );
    if (!pumpSpectatorRoom(
            [&spectatorHost, &fighter, &observer, epochBeforeLoad]() {
                const auto& handshakes =
                    spectatorHost.session().saveLoadHandshakes();
                return handshakes.size() == 2U
                    && handshakes.back().status
                        == RuntimeMultiplayerSaveLoadStatus::Committed
                    && spectatorHost.session().timelineEpoch()
                        == epochBeforeLoad + 1U
                    && spectatorHost.session().stateHash()
                        == fighter.session().stateHash()
                    && spectatorHost.session().stateHash()
                        == observer.session().stateHash();
            },
            ">5 MiB three-member automatic load commit",
            60000
        )) {
        return 1;
    }
    context.expect(
        spectatorHost.session().authoritativeTick()
                == restoredSnapshotTick + 1
            && fighter.session().authoritativeTick()
                == restoredSnapshotTick + 1
            && observer.session().authoritativeTick()
                == restoredSnapshotTick + 1
            && spectatorHostSnapshots.postCommits == 1
            && fighterSnapshots.postCommits == 1
            && observerSnapshots.postCommits == 1
            && spectatorHostSnapshots.postEpoch == epochBeforeLoad + 1U
            && fighterSnapshots.postEpoch == epochBeforeLoad + 1U
            && observerSnapshots.postEpoch == epochBeforeLoad + 1U
            && spectatorHostSnapshots.postTick
                == restoredSnapshotTick + 1
            && fighterSnapshots.postTick == restoredSnapshotTick + 1
            && observerSnapshots.postTick == restoredSnapshotTick + 1
            && spectatorHostDriver.timelineEpoch()
                == epochBeforeLoad + 1U
            && fighterDriver.timelineEpoch() == epochBeforeLoad + 1U
            && observerDriver.timelineEpoch() == epochBeforeLoad + 1U
            && spectatorHostDriver.currentBarrierTick()
                == restoredSnapshotTick + 1
            && fighterDriver.currentBarrierTick()
                == restoredSnapshotTick + 1
            && observerDriver.currentBarrierTick()
                == restoredSnapshotTick + 1
            && spectatorHostSnapshots.stagedBytes == spectatorLoadBytes
            && fighterSnapshots.stagedBytes == spectatorLoadBytes
            && observerSnapshots.stagedBytes == spectatorLoadBytes,
        "Load should roll back tick, advance epoch, rebind, and expose exact bytes"
    );

    const std::vector<std::uint8_t> postLoadFrame =
        encodedGameplayFrame(RuntimeMultiplayerGameplayFrame{}, context);
    context.expect(
        fighter.submitCommand(
            restoredSnapshotTick + 1,
            1,
            postLoadFrame
        ).succeeded,
        "public LAN command should resume in the new timeline epoch"
    );
    if (!pumpSpectatorRoom(
            [&spectatorHost, &fighter, &observer, epochBeforeLoad]() {
                return spectatorHost.session().commands().size() == 1U
                    && fighter.session().commands().size() == 1U
                    && observer.session().commands().size() == 1U
                    && spectatorHost.session().commands().front()
                        .timelineEpoch == epochBeforeLoad + 1U
                    && spectatorHost.session().stateHash()
                        == fighter.session().stateHash()
                    && spectatorHost.session().stateHash()
                        == observer.session().stateHash();
            },
            "post-load command uses current epoch"
        )) {
        return 1;
    }

    RuntimeMultiplayerCommandPacket oldEpochReplay =
        spectatorHost.session().commands().front();
    oldEpochReplay.timelineEpoch = epochBeforeLoad;
    RuntimeMultiplayerMessage oldEpochMessage;
    oldEpochMessage.type = RuntimeMultiplayerMessageType::Command;
    oldEpochMessage.body = oldEpochReplay;
    RuntimeNetworkPacket oldEpochPacket;
    error.clear();
    context.expect(
        encodeRuntimeMultiplayerMessage(
            oldEpochMessage,
            oldEpochPacket,
            error
        ),
        "old-epoch replay fixture should encode: " + error
    );
    RuntimeMultiplayerSessionState replayProbe = spectatorHost.session();
    const std::uint64_t replayHash = replayProbe.stateHash();
    const RuntimeMultiplayerMessageApplyResult replayed =
        applyRuntimeMultiplayerMessage(
            oldEpochPacket,
            "fighter-peer",
            replayProbe
        );
    context.expect(
        replayed.decoded && !replayed.accepted
            && replayProbe.stateHash() == replayHash,
        "old-epoch command replay is rejected atomically after Load"
    );

    const RuntimeMultiplayerLanOperationResult forbidden =
        observer.submitCommand(
            30,
            1,
            {0x43U, 0x47U}
        );
    context.expect(
        forbidden.succeeded && forbidden.queued,
        "opaque attacker transport may queue malformed frame bytes"
    );
    if (!pumpSpectatorRoom(
            [&spectatorHost, &fighter, &observer]() {
                return spectatorHost.phase()
                        == RuntimeMultiplayerLanPhase::Running
                    && fighter.phase()
                        == RuntimeMultiplayerLanPhase::Running
                    && observer.phase()
                        != RuntimeMultiplayerLanPhase::Running
                    && spectatorHost.session().members().size() == 2U
                    && fighter.session().members().size() == 2U;
            },
            "malformed spectator frame evicts only the observer"
        )) {
        return 1;
    }
    context.expect(
        spectatorHost.session().commands().size() == 1U
            && spectatorHost.session().commands().front().peerId
                == "fighter-peer"
            && spectatorHost.session().status()
                == RuntimeMultiplayerRoomStatus::Running
            && fighter.session().status()
                == RuntimeMultiplayerRoomStatus::Running
            && std::none_of(
                spectatorHost.session().members().begin(),
                spectatorHost.session().members().end(),
                [](const RuntimeMultiplayerRoomMember& member) {
                    return member.peerId == "observer-peer";
                }
            )
            && spectatorHost.session().stateHash()
                == fighter.session().stateHash(),
        "malformed spectator frame is atomic and canonically removes only "
        "the offending observer"
    );
    spectatorHost.close();
    fighter.close();
    observer.close();

    // A local stage failure occurs only after the proposal passes a Session
    // dry-run. It exposes no bytes, runs discard once, and aborts the room
    // without publishing a half-created handshake.
    RuntimeMultiplayerLanSession stageFailureHost;
    RuntimeMultiplayerLanSession stageFailureClient;
    SnapshotHookProbe stageFailureHostProbe;
    SnapshotHookProbe stageFailureClientProbe;
    stageFailureHostProbe.failStage = true;
    error.clear();
    context.expect(
        stageFailureHost.setSnapshotHooks(
            snapshotHooks(stageFailureHostProbe),
            error
        ) && stageFailureClient.setSnapshotHooks(
            snapshotHooks(stageFailureClientProbe),
            error
        ),
        "stage-failure snapshot hooks should configure: " + error
    );
    if (!startDirectTwoPeerRoom(
            stageFailureHost,
            stageFailureClient,
            "lan-snapshot-stage-failure",
            0x2a33445566778899ULL,
            40,
            context
        )) {
        return 1;
    }
    const std::vector<std::uint8_t> failureBytes(1025U, 0x3cU);
    RuntimeMultiplayerSaveLoadProposal stageFailureProposal =
        snapshotProposal(
            stageFailureHost.session(),
            1,
            RuntimeMultiplayerSaveLoadKind::Save,
            39,
            "stage-failure-save",
            failureBytes,
            context
        );
    const RuntimeMultiplayerLanOperationResult stageFailureResult =
        stageFailureHost.proposeSaveLoad(
            stageFailureProposal,
            failureBytes
        );
    context.expect(
        !stageFailureResult.succeeded
            && stageFailureHost.phase()
                == RuntimeMultiplayerLanPhase::Aborted
            && stageFailureHost.session().saveLoadHandshakes().empty()
            && stageFailureHostProbe.stages == 1
            && stageFailureHostProbe.commits == 0
            && stageFailureHostProbe.discards == 1
            && stageFailureHostProbe.stagedBytes.empty()
            && stageFailureClientProbe.stages == 0,
        "stage failure should be atomic and immediately room-fatal"
    );
    if (!pumpUntil(
            stageFailureHost,
            stageFailureClient,
            [&stageFailureClient]() {
                return stageFailureClient.phase()
                    == RuntimeMultiplayerLanPhase::Aborted;
            },
            context,
            "stage failure room abort replication"
        )) {
        return 1;
    }
    stageFailureHost.close();
    stageFailureClient.close();

    // A post-Prepare commit failure is no longer a rejectable handshake. The
    // failing client emits a negative Applied acknowledgement and every peer
    // terminates at CommitIssued instead of resuming play.
    RuntimeMultiplayerLanSession commitFailureHost;
    RuntimeMultiplayerLanSession commitFailureClient;
    SnapshotHookProbe commitFailureHostProbe;
    SnapshotHookProbe commitFailureClientProbe;
    commitFailureClientProbe.failCommit = true;
    error.clear();
    context.expect(
        commitFailureHost.setSnapshotHooks(
            snapshotHooks(commitFailureHostProbe),
            error
        ) && commitFailureClient.setSnapshotHooks(
            snapshotHooks(commitFailureClientProbe),
            error
        ),
        "commit-failure snapshot hooks should configure: " + error
    );
    if (!startDirectTwoPeerRoom(
            commitFailureHost,
            commitFailureClient,
            "lan-snapshot-commit-failure",
            0x2b33445566778899ULL,
            45,
            context
        )) {
        return 1;
    }
    RuntimeMultiplayerSaveLoadProposal commitFailureProposal =
        snapshotProposal(
            commitFailureHost.session(),
            1,
            RuntimeMultiplayerSaveLoadKind::Save,
            44,
            "commit-failure-save",
            failureBytes,
            context
        );
    context.expect(
        commitFailureHost.proposeSaveLoad(
            commitFailureProposal,
            failureBytes
        ).succeeded,
        "commit-failure transfer should start"
    );
    if (!pumpUntil(
            commitFailureHost,
            commitFailureClient,
            [&commitFailureHost, &commitFailureClient]() {
                return commitFailureHost.phase()
                        == RuntimeMultiplayerLanPhase::Aborted
                    && commitFailureClient.phase()
                        == RuntimeMultiplayerLanPhase::Aborted;
            },
            context,
            "negative Applied commit failure abort",
            15000
        )) {
        return 1;
    }
    context.expect(
        commitFailureHostProbe.stages == 1
            && commitFailureClientProbe.stages == 1
            && commitFailureHostProbe.commits == 1
            && commitFailureClientProbe.commits == 1
            && commitFailureHostProbe.discards == 1
            && commitFailureClientProbe.discards == 1
            && commitFailureHost.session().status()
                == RuntimeMultiplayerRoomStatus::Aborted
            && commitFailureClient.session().status()
                == RuntimeMultiplayerRoomStatus::Aborted
            && commitFailureHost.session().authoritativeTick() == 45
            && commitFailureClient.session().authoritativeTick() == 45,
        "commit failure should have no playable or half-advanced continuation"
    );
    commitFailureHost.close();
    commitFailureClient.close();

    RuntimeMultiplayerLanSession hostCommitFailureHost;
    RuntimeMultiplayerLanSession hostCommitFailureClient;
    SnapshotHookProbe hostCommitFailureHostProbe;
    SnapshotHookProbe hostCommitFailureClientProbe;
    hostCommitFailureHostProbe.failCommit = true;
    error.clear();
    context.expect(
        hostCommitFailureHost.setSnapshotHooks(
            snapshotHooks(hostCommitFailureHostProbe),
            error
        ) && hostCommitFailureClient.setSnapshotHooks(
            snapshotHooks(hostCommitFailureClientProbe),
            error
        ),
        "host-commit-failure hooks should configure: " + error
    );
    if (!startDirectTwoPeerRoom(
            hostCommitFailureHost,
            hostCommitFailureClient,
            "lan-host-snapshot-commit-failure",
            0x2c33445566778899ULL,
            47,
            context
        )) {
        return 1;
    }
    RuntimeMultiplayerSaveLoadProposal hostCommitFailureProposal =
        snapshotProposal(
            hostCommitFailureHost.session(),
            1,
            RuntimeMultiplayerSaveLoadKind::Save,
            46,
            "host-commit-failure-save",
            failureBytes,
            context
        );
    context.expect(
        hostCommitFailureHost.proposeSaveLoad(
            hostCommitFailureProposal,
            failureBytes
        ).succeeded,
        "host commit-failure transfer should start"
    );
    if (!pumpUntil(
            hostCommitFailureHost,
            hostCommitFailureClient,
            [&hostCommitFailureHost, &hostCommitFailureClient]() {
                return hostCommitFailureHost.phase()
                        == RuntimeMultiplayerLanPhase::Aborted
                    && hostCommitFailureClient.phase()
                        == RuntimeMultiplayerLanPhase::Aborted;
            },
            context,
            "host commit hook failure canonical abort",
            15000
        )) {
        return 1;
    }
    context.expect(
        hostCommitFailureHostProbe.stages == 1
            && hostCommitFailureClientProbe.stages == 1
            && hostCommitFailureHostProbe.commits == 1
            && hostCommitFailureClientProbe.commits == 1
            && hostCommitFailureHostProbe.discards == 1
            && hostCommitFailureClientProbe.discards == 1
            && hostCommitFailureHost.session().status()
                == RuntimeMultiplayerRoomStatus::Aborted
            && hostCommitFailureClient.session().status()
                == RuntimeMultiplayerRoomStatus::Aborted
            && hostCommitFailureHost.session().authoritativeTick() == 47
            && hostCommitFailureClient.session().authoritativeTick() == 47,
        "host commit hook failure should publish Commit barrier then abort"
    );
    hostCommitFailureHost.close();
    hostCommitFailureClient.close();

    // The post-advance callback is an acknowledgement, not another fallible
    // world operation. If it nevertheless fails, the host must stay at the
    // newly applied tick and terminate the room instead of rolling Session
    // backward and exposing a recoverable-looking half advance.
    RuntimeMultiplayerLanSession fatalAckHost;
    RuntimeMultiplayerLanSession fatalAckClient;
    RuntimeMultiplayerLanSimulationHooks incompleteHooks;
    incompleteHooks.onStarted = [](
        const RuntimeMultiplayerSessionState&,
        std::string&
    ) {
        return true;
    };
    error.clear();
    context.expect(
        !fatalAckHost.setSimulationHooks(
            std::move(incompleteHooks),
            error
        ),
        "partial simulation hook sets must be rejected"
    );
    int fatalAcknowledgements = 0;
    RuntimeMultiplayerLanSimulationHooks fatalHooks;
    fatalHooks.onStarted = [](
        const RuntimeMultiplayerSessionState&,
        std::string&
    ) {
        return true;
    };
    fatalHooks.onCommandAdmitted = [](
        const RuntimeMultiplayerCommandPacket&,
        std::string&
    ) {
        return true;
    };
    fatalHooks.prepareAuthoritativeAdvance = [](
        int,
        std::string&
    ) {
        return RuntimeMultiplayerLanSimulationAdvanceDecision::Ready;
    };
    fatalHooks.onAuthoritativeAdvance = [&fatalAcknowledgements](
        int,
        std::string& hookError
    ) {
        ++fatalAcknowledgements;
        hookError = "fixture acknowledgement failed";
        return false;
    };
    error.clear();
    context.expect(
        fatalAckHost.setSimulationHooks(std::move(fatalHooks), error),
        "complete fatal-ack fixture hooks should configure: " + error
    );
    int fatalClientAcknowledgements = 0;
    RuntimeMultiplayerLanSimulationHooks fatalClientHooks;
    fatalClientHooks.onStarted = [](
        const RuntimeMultiplayerSessionState&,
        std::string&
    ) {
        return true;
    };
    fatalClientHooks.onCommandAdmitted = [](
        const RuntimeMultiplayerCommandPacket&,
        std::string&
    ) {
        return true;
    };
    fatalClientHooks.prepareAuthoritativeAdvance = [](
        int,
        std::string&
    ) {
        return RuntimeMultiplayerLanSimulationAdvanceDecision::Ready;
    };
    fatalClientHooks.onAuthoritativeAdvance = [
        &fatalClientAcknowledgements
    ](
        int,
        std::string& hookError
    ) {
        ++fatalClientAcknowledgements;
        hookError = "fixture client acknowledgement failed";
        return false;
    };
    error.clear();
    context.expect(
        fatalAckClient.setSimulationHooks(
            std::move(fatalClientHooks),
            error
        ),
        "complete fatal client hooks should configure: " + error
    );
    if (!startDirectTwoPeerRoom(
            fatalAckHost,
            fatalAckClient,
            "lan-fatal-ack-room",
            0x33445566778899aaULL,
            50,
            context
        )) {
        return 1;
    }
    const RuntimeMultiplayerLanOperationResult fatalAdvance =
        fatalAckHost.advanceAuthoritativeTick(51);
    const RuntimeMultiplayerLanPollResult fatalEvents = fatalAckHost.poll(0);
    const auto fatalAdvancedEvent = std::find_if(
        fatalEvents.events.begin(),
        fatalEvents.events.end(),
        [](const RuntimeMultiplayerLanEvent& event) {
            return event.kind
                == RuntimeMultiplayerLanEventKind::AuthoritativeTickAdvanced;
        }
    );
    const auto fatalRejectedEvent = std::find_if(
        fatalEvents.events.begin(),
        fatalEvents.events.end(),
        [](const RuntimeMultiplayerLanEvent& event) {
            return event.kind
                == RuntimeMultiplayerLanEventKind::ProtocolRejected;
        }
    );
    const auto fatalAbortedEvent = std::find_if(
        fatalEvents.events.begin(),
        fatalEvents.events.end(),
        [](const RuntimeMultiplayerLanEvent& event) {
            return event.kind
                == RuntimeMultiplayerLanEventKind::SessionAborted;
        }
    );
    context.expect(
        !fatalAdvance.succeeded
            && fatalAdvance.applied
            && fatalAdvance.reason == "fixture acknowledgement failed"
            && fatalAcknowledgements == 1
            && fatalAckHost.phase() == RuntimeMultiplayerLanPhase::Aborted
            && fatalAckHost.session().status()
                == RuntimeMultiplayerRoomStatus::Aborted
            && fatalAckHost.session().authoritativeTick() == 51
            && fatalAckHost.session().abortState().tick == 51
            && fatalAckHost.session().abortState().reason
                == RuntimeMultiplayerSessionAbortReason::ProtocolViolation
            && fatalAdvancedEvent != fatalEvents.events.end()
            && fatalRejectedEvent != fatalEvents.events.end()
            && fatalAbortedEvent != fatalEvents.events.end()
            && fatalAdvancedEvent < fatalRejectedEvent
            && fatalRejectedEvent < fatalAbortedEvent,
        "post-advance acknowledgement failure should be ordered fatal state"
    );
    context.expect(
        !fatalAckHost.advanceAuthoritativeTick(52).succeeded,
        "a fatal acknowledgement failure must not be retryable"
    );
    if (!pumpUntil(
            fatalAckHost,
            fatalAckClient,
            [&fatalAckClient]() {
                return fatalAckClient.phase()
                    == RuntimeMultiplayerLanPhase::Aborted;
            },
            context,
            "fatal acknowledgement replication"
        )) {
        return 1;
    }
    context.expect(
        fatalAckClient.session().authoritativeTick() == 51,
        "fatal client should retain the newly applied authoritative tick: "
            + std::to_string(
                fatalAckClient.session().authoritativeTick()
            )
    );
    context.expect(
        fatalAckClient.session().status()
                == RuntimeMultiplayerRoomStatus::Aborted
            && fatalAckClient.session().abortState().tick == 51
            && fatalAckClient.session().abortState().reason
                == RuntimeMultiplayerSessionAbortReason::ProtocolViolation,
        "fatal client should abort at the newly applied tick: status="
            + std::to_string(static_cast<int>(
                fatalAckClient.session().status()
            ))
            + " abortTick="
            + std::to_string(fatalAckClient.session().abortState().tick)
            + " reason="
            + std::to_string(static_cast<int>(
                fatalAckClient.session().abortState().reason
            ))
    );
    context.expect(
        fatalClientAcknowledgements == 1,
        "client acknowledgement failure should run exactly once"
    );
    fatalAckHost.close();
    fatalAckClient.close();

    static_assert(
        runtimeMultiplayerLanMaximumDeferredCanonicalBytes
            <= runtimeMultiplayerMaximumBufferedCommandBytes
    );
    static_assert(
        runtimeMultiplayerLanMaximumDeferredCanonicalPackets > 0U
    );

    // Byte-bound fixture: a client keeps retrying the retained TickAdvance
    // while valid but large later commands arrive. They are never admitted;
    // exceeding the retained FIFO budget is a host protocol violation.
    RuntimeMultiplayerLanSession byteBoundHost;
    RuntimeMultiplayerLanSession byteBoundClient;
    int bytePrepareCalls = 0;
    int byteAcknowledgements = 0;
    error.clear();
    context.expect(
        byteBoundClient.setSimulationHooks(
            alwaysDeferredSimulationHooks(
                bytePrepareCalls,
                byteAcknowledgements
            ),
            error
        ),
        "byte-bound client hooks should configure: " + error
    );
    if (!startDirectTwoPeerRoom(
            byteBoundHost,
            byteBoundClient,
            "lan-deferred-byte-bound",
            0x445566778899aabbULL,
            60,
            context
        )) {
        return 1;
    }
    context.expect(
        byteBoundHost.advanceAuthoritativeTick(61).succeeded,
        "byte-bound host should publish the blocked advance"
    );
    context.expect(
        byteBoundHost.poll(1).succeeded,
        "byte-bound host should flush the blocked advance"
    );
    const RuntimeMultiplayerLanPollResult firstByteDeferred =
        byteBoundClient.poll(1000);
    context.expect(
        firstByteDeferred.succeeded
            && firstByteDeferred.simulationDeferred
            && byteBoundClient.session().authoritativeTick() == 60,
        "byte-bound client should retain the first advance"
    );
    const std::vector<std::uint8_t> maximumOpaquePayload(
        runtimeMultiplayerMaximumCommandPayloadBytes,
        0xa5U
    );
    bool byteLimitRejected = false;
    for (std::uint64_t sequence = 1;
         sequence <= 80U
            && byteBoundClient.phase()
                != RuntimeMultiplayerLanPhase::Aborted;
         ++sequence) {
        const RuntimeMultiplayerLanOperationResult command =
            byteBoundHost.submitCommand(
                61,
                sequence,
                maximumOpaquePayload
            );
        context.expect(
            command.succeeded && command.applied,
            "byte-bound host command should remain transport-valid: "
                + command.reason
        );
        if (!command.succeeded) break;
        for (int flush = 0; flush < 4
             && byteBoundClient.phase()
                != RuntimeMultiplayerLanPhase::Aborted;
             ++flush) {
            const RuntimeMultiplayerLanPollResult hostPoll =
                byteBoundHost.poll(1);
            const RuntimeMultiplayerLanPollResult clientPoll =
                byteBoundClient.poll(5);
            context.expect(
                hostPoll.succeeded && clientPoll.succeeded,
                "byte-bound fixture poll should stay operational"
            );
            byteLimitRejected = byteLimitRejected
                || std::any_of(
                    clientPoll.events.begin(),
                    clientPoll.events.end(),
                    [](const RuntimeMultiplayerLanEvent& event) {
                        return event.kind
                                == RuntimeMultiplayerLanEventKind::ProtocolRejected
                            && event.reason.find(
                                "deferred canonical packet queue limits"
                            ) != std::string::npos;
                    }
                );
        }
    }
    context.expect(
        byteLimitRejected
            && byteBoundClient.phase()
                == RuntimeMultiplayerLanPhase::Aborted
            && byteBoundClient.session().status()
                == RuntimeMultiplayerRoomStatus::Aborted
            && byteBoundClient.session().authoritativeTick() == 60
            && byteBoundClient.session().commands().empty()
            && byteBoundClient.session().abortState().reason
                == RuntimeMultiplayerSessionAbortReason::ProtocolViolation
            && bytePrepareCalls > 0
            && byteAcknowledgements == 0,
        "deferred FIFO byte overflow should close without canonical mutation"
    );
    byteBoundHost.close();
    byteBoundClient.close();

    // Packet-count fixture uses tiny idempotent advances, proving the second
    // limit is independent of retained byte volume.
    RuntimeMultiplayerLanSession packetBoundHost;
    RuntimeMultiplayerLanSession packetBoundClient;
    int packetPrepareCalls = 0;
    int packetAcknowledgements = 0;
    error.clear();
    context.expect(
        packetBoundClient.setSimulationHooks(
            alwaysDeferredSimulationHooks(
                packetPrepareCalls,
                packetAcknowledgements
            ),
            error
        ),
        "packet-bound client hooks should configure: " + error
    );
    if (!startDirectTwoPeerRoom(
            packetBoundHost,
            packetBoundClient,
            "lan-deferred-packet-bound",
            0x5566778899aabbccULL,
            70,
            context
        )) {
        return 1;
    }
    context.expect(
        packetBoundHost.advanceAuthoritativeTick(71).succeeded
            && packetBoundHost.poll(1).succeeded,
        "packet-bound host should publish the blocked advance"
    );
    const RuntimeMultiplayerLanPollResult firstPacketDeferred =
        packetBoundClient.poll(1000);
    context.expect(
        firstPacketDeferred.succeeded
            && firstPacketDeferred.simulationDeferred,
        "packet-bound client should retain the first advance"
    );
    bool packetLimitRejected = false;
    for (std::size_t index = 0;
         index < runtimeMultiplayerLanMaximumDeferredCanonicalPackets
            && packetBoundClient.phase()
                != RuntimeMultiplayerLanPhase::Aborted;
         ++index) {
        const RuntimeMultiplayerLanOperationResult duplicate =
            packetBoundHost.advanceAuthoritativeTick(71);
        context.expect(
            duplicate.succeeded && duplicate.applied && duplicate.duplicate,
            "packet-bound duplicate advance should be valid"
        );
        if ((index + 1U) % 64U == 0U) {
            const RuntimeMultiplayerLanPollResult hostPoll =
                packetBoundHost.poll(1);
            const RuntimeMultiplayerLanPollResult clientPoll =
                packetBoundClient.poll(10);
            context.expect(
                hostPoll.succeeded && clientPoll.succeeded,
                "packet-bound fixture poll should stay operational"
            );
            packetLimitRejected = packetLimitRejected
                || std::any_of(
                    clientPoll.events.begin(),
                    clientPoll.events.end(),
                    [](const RuntimeMultiplayerLanEvent& event) {
                        return event.kind
                                == RuntimeMultiplayerLanEventKind::ProtocolRejected
                            && event.reason.find(
                                "deferred canonical packet queue limits"
                            ) != std::string::npos;
                    }
                );
        }
    }
    const auto packetLimitDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < packetLimitDeadline
        && packetBoundClient.phase() != RuntimeMultiplayerLanPhase::Aborted) {
        const RuntimeMultiplayerLanPollResult hostPoll =
            packetBoundHost.poll(1);
        const RuntimeMultiplayerLanPollResult clientPoll =
            packetBoundClient.poll(10);
        context.expect(
            hostPoll.succeeded && clientPoll.succeeded,
            "packet-bound drain should stay operational"
        );
        packetLimitRejected = packetLimitRejected
            || std::any_of(
                clientPoll.events.begin(),
                clientPoll.events.end(),
                [](const RuntimeMultiplayerLanEvent& event) {
                    return event.kind
                            == RuntimeMultiplayerLanEventKind::ProtocolRejected
                        && event.reason.find(
                            "deferred canonical packet queue limits"
                        ) != std::string::npos;
                }
            );
    }
    context.expect(
        packetLimitRejected
            && packetBoundClient.phase()
                == RuntimeMultiplayerLanPhase::Aborted
            && packetBoundClient.session().authoritativeTick() == 70
            && packetBoundClient.session().commands().empty()
            && packetBoundClient.session().abortState().reason
                == RuntimeMultiplayerSessionAbortReason::ProtocolViolation
            && packetPrepareCalls > 0
            && packetAcknowledgements == 0,
        "deferred FIFO packet overflow should close without canonical mutation"
    );
    packetBoundHost.close();
    packetBoundClient.close();

    if (!testPasswordProtectedAdmission(context)
        || !testRunningSpectatorLifecycle(context)
        || !testReconnectTimeoutTerminates(context)) {
        return 1;
    }

    if (context.failures != 0) {
        std::cerr << "Runtime multiplayer LAN flow smoke failed with "
                  << context.failures << " failure(s).\n";
        return 1;
    }
    std::cout << "Runtime multiplayer LAN flow smoke passed.\n";
    return 0;
}
