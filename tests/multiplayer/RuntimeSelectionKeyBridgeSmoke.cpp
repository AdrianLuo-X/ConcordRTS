#include "ui/input/RuntimeSelectionKeyController.h"
#include "network/lockstep/RuntimeMultiplayerPlayerIntent.h"
#include "network/lockstep/RuntimeMultiplayerRuntimeBridge.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
struct SmokeContext {
    int checks = 0;
    int failures = 0;
    void expect(bool condition, const std::string& message) {
        ++checks;
        if (!condition) { ++failures; std::cerr << "FAIL selection-key bridge: " << message << '\n'; }
    }
};
struct ObservedWorld {
    int tick = 0;
    std::vector<RuntimeMultiplayerLockstepApplyCommand> applied;
    std::vector<std::pair<char, int>> order;
    // Owned wire envelopes copied before Session retires this tick's packets.
    std::vector<RuntimeMultiplayerCommandPacket> observedFrames;
};
std::uint64_t observerHash(const ObservedWorld& world) {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&](std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) { hash ^= static_cast<std::uint8_t>(value >> shift); hash *= 1099511628211ULL; }
    };
    mix(static_cast<std::uint64_t>(world.tick));
    for (const auto& command : world.applied) {
        mix(static_cast<std::uint64_t>(command.tick)); mix(command.frameSequence); mix(command.commandIndex);
        for (unsigned char byte : command.peerId) mix(byte);
        std::vector<std::uint8_t> bytes; std::string error;
        if (!encodeRuntimeMultiplayerGameplayCommand(command.command, bytes, error)) return 0;
        for (auto byte : bytes) mix(byte);
    }
    return hash == 0 ? 1 : hash;
}
RuntimeMultiplayerRuntimeCallbacks callbacksFor(ObservedWorld& world, const RuntimeMultiplayerRuntimeBridge& bridge) {
    RuntimeMultiplayerRuntimeCallbacks callbacks;
    callbacks.prevalidateCommands = [&world,&bridge](std::span<const RuntimeMultiplayerLockstepApplyCommand> batch, std::string& error) {
        // RuntimeBridge invokes this from prepareAuthoritativeAdvance, before
        // LanSession installs its prevalidated next Session. Live commands()
        // therefore still owns the admitted wire envelopes for this barrier.
        // Copy only; do not reconstruct packets from the captured intent list.
        for (const auto& packet : bridge.lan().session().commands()) {
            if (packet.tick == world.tick + 1
                && std::find(world.observedFrames.begin(), world.observedFrames.end(), packet) == world.observedFrames.end())
                world.observedFrames.push_back(packet);
        }
        for (const auto& item : batch) if (item.tick != world.tick + 1) { error = "command applied outside next original tick"; return false; }
        error.clear(); return true;
    };
    callbacks.applyCommands = [&world](std::span<const RuntimeMultiplayerLockstepApplyCommand> batch, std::string& error) {
        world.order.emplace_back('A', world.tick + 1);
        world.applied.insert(world.applied.end(), batch.begin(), batch.end()); error.clear(); return true;
    };
    callbacks.currentCompletedTick = [&world]() { return world.tick; };
    callbacks.advanceSimulation = [&world](int tick) {
        if (tick != world.tick + 1 || world.order.empty() || world.order.back() != std::pair<char,int>{'A',tick})
            throw std::logic_error("bridge advanced before same-tick application");
        world.order.emplace_back('V', tick); world.tick = tick;
    };
    callbacks.authoritativeStateHash = [&world]() { return observerHash(world); };
    callbacks.matchTerminal = []() { return false; };
    // Required attachment ports are present, but snapshot behavior is outside this test.
    callbacks.stageVerifiedSnapshot = [](const RuntimeMultiplayerSnapshotDescriptor&, std::span<const std::uint8_t>, std::uint64_t&, std::string& error) { error="snapshot not exercised by selection-key bridge fixture"; return false; };
    callbacks.commitStagedSnapshot = [](const RuntimeMultiplayerSnapshotDescriptor&, std::uint64_t&, std::string& error) { error="snapshot not exercised"; return false; };
    callbacks.discardStagedSnapshot = [](const RuntimeMultiplayerSnapshotDescriptor&) {};
    callbacks.onLoadTimelineRebound = [](const RuntimeMultiplayerSnapshotDescriptor&, const RuntimeMultiplayerSessionState&, std::string& error) { error="load not exercised"; return false; };
    return callbacks;
}
RuntimeMultiplayerSessionConfig sessionConfig() {
    RuntimeMultiplayerSessionConfig config;
    config.roomId = "runtime-bridge-room";
    config.hostPeerId = "host-peer";
    config.hostDisplayName = "Host";
    config.compatibility.protocolFingerprint = 0x1020304050607080ULL;
    config.compatibility.contentFingerprint = 0x8877665544332211ULL;
    config.stateHashPeriodTicks = 1;
    return config;
}

RuntimeLanRoomAdvertisement advertisementFor(
    const RuntimeMultiplayerRuntimeBridge& host
) {
    RuntimeLanRoomAdvertisement room;
    room.roomId = host.lan().session().config().roomId;
    room.roomName = "Runtime Bridge";
    room.hostName = "Host";
    room.hostAddress = "127.0.0.1";
    room.contentFingerprint = "8877665544332211";
    room.protocolVersion = runtimeLanProtocolVersion;
    room.sessionPort = host.lan().sessionPort();
    room.playerCount = 1;
    room.maximumPlayers = static_cast<std::uint8_t>(
        host.lan().session().config().maximumMembers
    );
    return room;
}

RuntimeMultiplayerMatchSetup matchSetup() {
    RuntimeMultiplayerMatchSetup setup;
    setup.revision = 1;
    setup.hostPeerId = "host-peer";
    setup.map.canonicalId = "maps/runtime-bridge.map";
    setup.map.contentFingerprint = 0x8877665544332211ULL;
    setup.rules.gameMode = "battle";
    setup.rules.rulesOverride = "mpbattlemd.ini";
    setup.rules.randomSeed = 0x3141592653589793ULL;
    setup.rules.fixedTickHz = 30;
    setup.rules.startingCredits = 10000;
    setup.rules.startingUnits = 10;
    setup.rules.techLevel = 10;
    setup.rules.gameSpeed = 2;

    RuntimeMultiplayerMatchMember client;
    client.peerId = "client-peer";
    client.displayName = "Client";
    client.identity = RuntimeMultiplayerMatchMemberIdentity::Combatant;
    RuntimeMultiplayerMatchMember host;
    host.peerId = "host-peer";
    host.displayName = "Host";
    host.identity = RuntimeMultiplayerMatchMemberIdentity::Combatant;
    setup.members = {client, host};

    RuntimeMultiplayerMatchSlot hostSlot;
    hostSlot.slotIndex = 0;
    hostSlot.controller = RuntimeMultiplayerMatchSlotController::Human;
    hostSlot.peerId = "host-peer";
    hostSlot.countryId = "random";
    hostSlot.colorId = "red";
    hostSlot.teamId = "-";
    hostSlot.difficultyId = "medium";
    hostSlot.startPosition = 1;
    RuntimeMultiplayerMatchSlot clientSlot = hostSlot;
    clientSlot.slotIndex = 1;
    clientSlot.peerId = "client-peer";
    clientSlot.colorId = "blue";
    clientSlot.startPosition = 2;
    setup.slots = {hostSlot, clientSlot};
    return setup;
}


bool pumpUntil(
    RuntimeMultiplayerRuntimeBridge& host,
    RuntimeMultiplayerRuntimeBridge& client,
    const std::function<bool()>& complete,
    SmokeContext& context,
    const std::string& description,
    int timeoutMilliseconds = 5000
) {
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMilliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const RuntimeMultiplayerRuntimePumpResult hostPoll = host.pump(false);
        const RuntimeMultiplayerRuntimePumpResult clientPoll = client.pump(false);
        if (!hostPoll.succeeded || !clientPoll.succeeded) {
            context.expect(
                false,
                description + ": pump failed: "
                    + (!hostPoll.succeeded ? hostPoll.error : clientPoll.error)
            );
            return false;
        }
        if (complete()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(false, description + ": timed out");
    return false;
}

bool startRunningPair(
    RuntimeMultiplayerRuntimeBridge& host,
    RuntimeMultiplayerRuntimeBridge& client,
    SmokeContext& context,
    std::uint64_t roomGeneration,
    const std::string& description
) {
    std::string error;
    RuntimeMultiplayerLanHostOptions hostOptions;
    hostOptions.session = sessionConfig();
    hostOptions.roomName = "Runtime Bridge Deadline";
    hostOptions.discoveryPort = 0;
    hostOptions.sessionPort = 0;
    hostOptions.roomGeneration = roomGeneration;
    if (!host.lan().startHost(hostOptions, error)) {
        context.expect(false, description + ": host start: " + error);
        return false;
    }

    RuntimeMultiplayerLanJoinOptions join;
    join.room = advertisementFor(host);
    join.peerId = "client-peer";
    join.displayName = "Client";
    join.compatibility = hostOptions.session.compatibility;
    join.connectTimeoutMilliseconds = 2000;
    if (!client.lan().join(join, error)) {
        context.expect(false, description + ": client join: " + error);
        return false;
    }
    if (!pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.lan().phase() == RuntimeMultiplayerLanPhase::Lobby
                    && client.lan().phase()
                        == RuntimeMultiplayerLanPhase::Lobby
                    && host.lan().session().members().size() == 2U
                    && client.lan().session().members().size() == 2U;
            },
            context,
            description + " admission"
        )) {
        return false;
    }

    const RuntimeMultiplayerLanOperationResult setup =
        host.lan().setMatchSetup(matchSetup());
    if (!setup.succeeded) {
        context.expect(false, description + ": setup: " + setup.reason);
        return false;
    }
    if (!pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.lan().session().matchSetup().has_value()
                    && client.lan().session().matchSetup().has_value();
            },
            context,
            description + " setup"
        )) {
        return false;
    }

    const RuntimeMultiplayerLanOperationResult hostReady =
        host.lan().setReady(true);
    const RuntimeMultiplayerLanOperationResult clientReady =
        client.lan().setReady(true);
    if (!hostReady.succeeded || !clientReady.succeeded
        || !pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.lan().session().allMembersReady()
                    && client.lan().session().allMembersReady();
            },
            context,
            description + " ready"
        )) {
        context.expect(
            hostReady.succeeded && clientReady.succeeded,
            description + ": ready intent failed"
        );
        return false;
    }

    const RuntimeMultiplayerLanOperationResult started =
        host.lan().startMatch(1);
    if (!started.succeeded) {
        context.expect(false, description + ": start: " + started.reason);
        return false;
    }
    return pumpUntil(
        host,
        client,
        [&host, &client]() {
            return host.lan().phase() == RuntimeMultiplayerLanPhase::Running
                && client.lan().phase()
                    == RuntimeMultiplayerLanPhase::Running
                && host.lockstepBound() && client.lockstepBound();
        },
        context,
        description + " start"
    );
}


struct ControllerHost final : RuntimeSelectionKeyHost {
    RuntimeMultiplayerPlayerIntentRouter& router;
    std::vector<std::int32_t> actors{9,4,9};
    std::size_t samples = 0, dispatches = 0;
    bool accepted = false;
    std::string error;
    explicit ControllerHost(RuntimeMultiplayerPlayerIntentRouter& value) : router(value) {}
    std::int32_t binding(RuntimeSelectionKeyBinding id, std::int32_t fallback) override {
        switch(id) {
        case RuntimeSelectionKeyBinding::Stop: return 'S';
        case RuntimeSelectionKeyBinding::Guard: return 'G';
        case RuntimeSelectionKeyBinding::Scatter: return 'X';
        case RuntimeSelectionKeyBinding::Deploy: return 'D';
        }
        return fallback;
    }
    std::int32_t sampleKeyCode(const RuntimeSelectionKeyInput& input) override { ++samples; return input.rawKey; }
    void dispatch(RuntimeSelectionKeyAction action) override {
        ++dispatches;
        switch(action) {
        case RuntimeSelectionKeyAction::Stop: accepted=router.stop(actors,error); break;
        case RuntimeSelectionKeyAction::Guard: accepted=router.guard(actors,error); break;
        case RuntimeSelectionKeyAction::Scatter: accepted=router.scatter(actors,error); break;
        case RuntimeSelectionKeyAction::Deploy: accepted=router.secondaryAction(RuntimeMultiplayerGameplaySecondaryActionKind::Contextual,actors,error); break;
        }
    }
};
bool runDelay(SmokeContext& context, int delay) {
    const int beforeFailures=context.failures;
    RuntimeMultiplayerLockstepConfig config; config.inputDelayTicks=delay; config.maximumCommandsPerFrame=4;
    RuntimeMultiplayerRuntimeBridge host(config), client(config);
    if(!startRunningPair(host,client,context,0x5730303900000000ULL+static_cast<std::uint64_t>(delay),"selection keys delay "+std::to_string(delay))) return false;
    ObservedWorld hostWorld,clientWorld;
    std::string error;
    context.expect(host.attachRuntime(callbacksFor(hostWorld,host),error),"host attach: "+error);
    error.clear(); context.expect(client.attachRuntime(callbacksFor(clientWorld,client),error),"client attach: "+error);
    if(context.failures!=beforeFailures) return false;
    if(!pumpUntil(host,client,[&](){return host.initialStateHashVerified()&&client.initialStateHashVerified();},context,"initial hash")) return false;
    const int captureTick=hostWorld.tick;
    const int captureBarrier=host.lockstep().currentBarrierTick();
    int lastAuthoredTick=-1; std::uint64_t lastSequence=0;
    for(const auto& packet:host.lan().session().commands()) if(packet.peerId=="host-peer") {
        lastAuthoredTick=std::max(lastAuthoredTick,packet.tick); lastSequence=std::max(lastSequence,packet.sequence);
    }
    const int expectedFrameTick=std::max(captureBarrier+delay,lastAuthoredTick+1);
    std::vector<RuntimeMultiplayerGameplayCommand> captured;
    std::vector<int> capturedAt;
    RuntimeMultiplayerPlayerIntentRouter router([&](const RuntimeMultiplayerGameplayCommand& command,std::string& why) {
        const bool accepted=host.captureGameplayCommand(command,why);
        if(accepted) { captured.push_back(command); capturedAt.push_back(hostWorld.tick); }
        return accepted;
    });
    ControllerHost inputHost(router);
    const char keys[]={'S','G','X','D'};
    for(std::size_t index=0;index<4;++index) {
        const auto beforeSamples=inputHost.samples;
        const auto result=handleRuntimeSelectionKey({keys[index],false,RuntimeKeyDownSource::KeyDown},inputHost);
        context.expect(result==RuntimeSelectionKeyResult::DispatchedConsumed&&inputHost.accepted,"same production controller forwards action "+std::to_string(index));
        context.expect(inputHost.samples-beforeSamples==index+2,"original 2/3/4/5 key samples");
        context.expect(host.pendingGameplayCommandCount()==index+1&&hostWorld.tick==captureTick&&hostWorld.applied.empty(),"capture only queues, never advances");
    }
    const std::vector<std::int32_t> canonicalActors{4,9};
    if(captured.size()==4) {
        const auto* stop=std::get_if<RuntimeMultiplayerGameplayStopCommand>(&captured[0].body);
        const auto* guard=std::get_if<RuntimeMultiplayerGameplayGuardCommand>(&captured[1].body);
        const auto* scatter=std::get_if<RuntimeMultiplayerGameplayScatterCommand>(&captured[2].body);
        const auto* deploy=std::get_if<RuntimeMultiplayerGameplaySecondaryActionCommand>(&captured[3].body);
        context.expect(stop&&stop->objectIds==canonicalActors,"Stop canonical selected actors");
        context.expect(guard&&guard->objectIds==canonicalActors,"Guard canonical selected actors");
        context.expect(scatter&&scatter->objectIds==canonicalActors,"Scatter canonical selected actors");
        context.expect(deploy&&deploy->objectIds==canonicalActors&&deploy->action==RuntimeMultiplayerGameplaySecondaryActionKind::Contextual,"Deploy canonical selected actors and action");
    }
    const auto dispatches=inputHost.dispatches;
    context.expect(handleRuntimeSelectionKey({'S',true,RuntimeKeyDownSource::SystemKeyDown},inputHost)==RuntimeSelectionKeyResult::RepeatedConsumed
        &&inputHost.dispatches==dispatches&&host.pendingGameplayCommandCount()==4,"repeat consumes without new bridge command");
    // Real bridge frame-capacity rejection, reached through the actual intent router.
    context.expect(handleRuntimeSelectionKey({'S',false,RuntimeKeyDownSource::KeyDown},inputHost)==RuntimeSelectionKeyResult::DispatchedConsumed
        &&!inputHost.accepted&&host.pendingGameplayCommandCount()==4,"bridge full rejection preserves consumed action and pending commands");
    for(int mode=0;mode<3;++mode) {
        RuntimeMultiplayerPlayerIntentAuthorityState authority;
        authority.spectator=mode==0; authority.terminal=mode==1; authority.localDefeated=mode==2;
        router.setAuthorityState(authority);
        const auto count=captured.size();
        context.expect(handleRuntimeSelectionKey({'G',false,RuntimeKeyDownSource::KeyDown},inputHost)==RuntimeSelectionKeyResult::DispatchedConsumed
            &&!inputHost.accepted&&captured.size()==count,"actual router authority rejection "+std::to_string(mode));
    }
    router.setAuthorityState({});
    context.expect(captured.size()==4&&std::all_of(capturedAt.begin(),capturedAt.end(),[&](int tick){return tick==captureTick;}),"all canonical captures retain original runtime tick");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    bool complete=false;
    while(std::chrono::steady_clock::now()<deadline) {
        const auto hp=host.pump(true); const auto cp=client.pump(false);
        if(!hp.succeeded||!cp.succeeded) { context.expect(false,"real pump: "+(!hp.succeeded?hp.error:cp.error)); break; }
        complete=hostWorld.applied.size()==4&&clientWorld.applied.size()==4
            &&hostWorld.tick>=expectedFrameTick&&clientWorld.tick>=expectedFrameTick
            &&hostWorld.tick==clientWorld.tick&&observerHash(hostWorld)==observerHash(clientWorld);
        if(complete) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(complete,"real frame reaches apply and fixed advance for delay "+std::to_string(delay));
    context.expect(hostWorld.applied==clientWorld.applied,"both peers receive identical canonical command envelopes");
    std::vector<RuntimeMultiplayerCommandPacket> authored, replicated;
    for(const auto& packet:hostWorld.observedFrames) if(packet.peerId=="host-peer"&&packet.tick==expectedFrameTick) authored.push_back(packet);
    for(const auto& packet:clientWorld.observedFrames) if(packet.peerId=="host-peer"&&packet.tick==expectedFrameTick) replicated.push_back(packet);
    context.expect(authored.size()==1,"exact first unsent local frame tick observed before retirement");
    context.expect(replicated.size()==1,"client admitted exact authored wire frame before retirement");
    if(authored.size()==1&&replicated.size()==1) {
        RuntimeMultiplayerGameplayFrame frame, replica;
        context.expect(decodeRuntimeMultiplayerGameplayFrame(authored.front().payload,frame,error,4)&&frame.commands==captured,"observed host wire frame preserves canonical payload order");
        context.expect(decodeRuntimeMultiplayerGameplayFrame(replicated.front().payload,replica,error,4)&&replica==frame,"independently received client wire frame preserves every command");
        context.expect(authored.front()==replicated.front(),"host and client observed identical actual wire envelopes");
        context.expect(authored.front().sequence==lastSequence+1,"frame sequence follows original authored frame");
        std::ostringstream wire;
        for(auto byte:authored.front().payload) wire<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(byte);
        std::cout<<"selection-key bridge wire delay="<<delay<<" tick="<<authored.front().tick<<" peer="<<authored.front().peerId
            <<" sequence="<<authored.front().sequence<<" timelineEpoch="<<authored.front().timelineEpoch<<" payload="<<wire.str()<<'\n';
    }
    for(std::size_t index=0;index<hostWorld.applied.size();++index) {
        const auto& item=hostWorld.applied[index];
        context.expect(index<captured.size()&&item.command==captured[index]&&item.peerId=="host-peer"
            &&item.tick==expectedFrameTick&&item.frameSequence==lastSequence+1&&item.commandIndex==index,"canonical owner/actor/order/tick/sequence/index "+std::to_string(index));
        std::vector<std::uint8_t> payload; encodeRuntimeMultiplayerGameplayCommand(item.command,payload,error);
        std::ostringstream hex; for(auto byte:payload) hex<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(byte);
        std::cout<<"selection-key bridge trace delay="<<delay<<" captureTick="<<captureTick<<" captureBarrier="<<captureBarrier
            <<" frameTick="<<item.tick<<" peer="<<item.peerId<<" frameSequence="<<item.frameSequence<<" commandIndex="<<item.commandIndex<<" payload="<<hex.str()<<'\n';
    }
    context.expect(hostWorld.tick==clientWorld.tick&&observerHash(hostWorld)==observerHash(clientWorld),"bridge observer states agree after apply/advance");
    context.expect(hostWorld.order.size()%2==0&&clientWorld.order.size()%2==0,"every committed tick has one apply before one advance");
    for(const auto& entry:hostWorld.order) std::cout<<"selection-key bridge order delay="<<delay<<" peer=host stage="<<entry.first<<" tick="<<entry.second<<'\n';
    for(const auto& entry:clientWorld.order) std::cout<<"selection-key bridge order delay="<<delay<<" peer=client stage="<<entry.first<<" tick="<<entry.second<<'\n';
    host.detachRuntime(); client.detachRuntime(); host.lan().close(); client.lan().close();
    (void)consumeRuntimeMultiplayerVisibleFailure();
    return context.failures==beforeFailures;
}
} // namespace
int main() {
    SmokeContext context;
    (void)runDelay(context,0); (void)runDelay(context,2);
    if(context.failures) return 1;
    std::cout<<"Selection-key bridge passed: checks="<<context.checks
        <<" delays=0,2 actions=4 repeat=1 bridgeCapacityReject=1 routerAuthorityReject=3 realLoopbackBridge=1"
        <<" applyBeforeAdvance=1 shellWorld=not-covered fullSnapshot=not-run externalNetwork=not-run\n";
    return 0;
}
