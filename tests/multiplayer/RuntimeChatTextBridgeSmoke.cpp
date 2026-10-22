#include "ui/input/RuntimeChatTextController.h"
#include "network/lockstep/RuntimeMultiplayerPlayerIntent.h"
#include "network/lockstep/RuntimeMultiplayerRuntimeBridge.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
        if (!condition) { ++failures; std::cerr << "FAIL chat-text bridge: " << message << '\n'; }
    }
};
struct ObservedWorld {
    int tick = 0;
    std::vector<RuntimeMultiplayerLockstepApplyCommand> applied;
    std::vector<std::pair<char, int>> order;
    // Owned wire envelopes copied before Session retires this tick's packets.
    std::vector<RuntimeMultiplayerCommandPacket> observedFrames;
    std::vector<std::vector<RuntimeMultiplayerLockstepApplyCommand>> prevalidated;
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
        world.prevalidated.emplace_back(batch.begin(),batch.end());
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
    callbacks.stageVerifiedSnapshot = [](const RuntimeMultiplayerSnapshotDescriptor&, std::span<const std::uint8_t>, std::uint64_t&, std::string& error) { error="snapshot not exercised by chat-text bridge fixture"; return false; };
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
    hostOptions.roomName = "Chat Text Bridge";
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



using Communication=RuntimeMultiplayerGameplayPlayerCommunicationCommand;
using Cell=RuntimeMultiplayerGameplayCell;
struct ChatHost final:RuntimeChatTextHost {
    RuntimeMultiplayerPlayerIntentRouter& router;RuntimePlayerCommunicationState& ui;
    bool selectedValid=true,accepted=false;std::uint8_t mask=0;
    std::size_t dispatches=0,repaints=0,invalidations=0;std::string error;
    ChatHost(RuntimeMultiplayerPlayerIntentRouter& r,RuntimePlayerCommunicationState& s):router(r),ui(s){}
    bool menuOpen()override{return false;}
    bool selectedBeaconValid()override{if(!selectedValid)ui.selectedBeacon.reset();return selectedValid;}
    Cell selectedBeaconCell()override{return *ui.selectedBeacon;}
    std::uint8_t outgoingChatMask()override{return mask;}
    bool dispatchCommunication(Communication value)override{++dispatches;accepted=router.communicate(std::move(value),error);return accepted;}
    void requestRepaint()override{++repaints;}
    void invalidate()override{++invalidations;}
};
RuntimeMultiplayerGameplayCommand expectedCommand(RuntimePlayerCommunicationKind kind,const std::string& text,
    std::uint8_t mask=0,Cell cell={}) {
    Communication value;value.kind=kind;value.text=text;value.value=mask;value.cell=cell;
    RuntimeMultiplayerGameplayCommand command;command.body=std::move(value);return command;
}
std::vector<RuntimeMultiplayerGameplayCommand> expectedHost() {
    using Kind=RuntimePlayerCommunicationKind;
    return {expectedCommand(Kind::ChatAll,"all"),
        expectedCommand(Kind::ChatAllies,"\xe4\xb8\xad\xe6\x96\x87",3),
        expectedCommand(Kind::ChatAll,"self",1),
        expectedCommand(Kind::ChatAllies,"\xf0\x9f\x98\x80",3),
        expectedCommand(Kind::BeaconText,"marker",0,{9,11}),
        expectedCommand(Kind::BeaconText,"",0,{9,11}),
        expectedCommand(Kind::ChatAll,std::string(256,'x')),
        expectedCommand(Kind::ChatAll,"trimmed",3)};
}
std::vector<RuntimeMultiplayerGameplayCommand> expectedClient() {
    using Kind=RuntimePlayerCommunicationKind;
    return {expectedCommand(Kind::ChatAll,"client-all"),
        expectedCommand(Kind::ChatAllies,"client allies",3),
        expectedCommand(Kind::BeaconText,"client-marker",0,{7,13})};
}
bool enterText(ChatHost& host,std::u16string_view text,bool allies,std::uint8_t mask,
    bool beacon=false,Cell selected={9,11},bool prefilled=false) {
    host.ui={};host.ui.chatOpen=true;host.ui.chatAllies=allies;host.ui.beaconEditing=beacon;
    host.ui.selectedBeacon=selected;host.mask=mask;host.selectedValid=true;
    if(prefilled)host.ui.chatDraft.assign(text);
    else for(char16_t value:text)if(!handleRuntimeChatTextCharacter({static_cast<std::uint64_t>(value)},host.ui,host))return false;
    return handleRuntimeChatTextKey({13,false},host.ui,host);
}
std::string hex(std::span<const std::uint8_t> data) {
    std::ostringstream out;for(auto byte:data)out<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(byte);return out.str();
}
bool runDelay(SmokeContext& context,int delay) {
    const int beforeFailures=context.failures;constexpr std::size_t hostCount=8,clientCount=3,totalCount=hostCount+clientCount;
    const auto expectedA=expectedHost(),expectedB=expectedClient();std::ostringstream evidence;
    RuntimeMultiplayerLockstepConfig config;config.inputDelayTicks=delay;config.maximumCommandsPerFrame=hostCount;
    RuntimeMultiplayerRuntimeBridge host(config),client(config);
    if(!startRunningPair(host,client,context,0x5730313101000000ULL+static_cast<std::uint64_t>(delay),"chat text delay "+std::to_string(delay)))return false;
    ObservedWorld hostWorld,clientWorld;std::string error;
    context.expect(host.attachRuntime(callbacksFor(hostWorld,host),error),"chat host attach: "+error);
    error.clear();context.expect(client.attachRuntime(callbacksFor(clientWorld,client),error),"chat client attach: "+error);
    if(context.failures!=beforeFailures)return false;
    if(!pumpUntil(host,client,[&]{return host.initialStateHashVerified()&&client.initialStateHashVerified();},context,"chat initial hash"))return false;
    const int captureTick=hostWorld.tick,captureBarrier=host.lockstep().currentBarrierTick();
    int previousHostTick=-1,previousClientTick=-1;std::uint64_t hostSequence=0,clientSequence=0;
    for(const auto& packet:host.lan().session().commands()){
        if(packet.peerId=="host-peer"){previousHostTick=std::max(previousHostTick,packet.tick);hostSequence=std::max(hostSequence,packet.sequence);}
    }
    for(const auto& packet:client.lan().session().commands())if(packet.peerId=="client-peer"){
        previousClientTick=std::max(previousClientTick,packet.tick);clientSequence=std::max(clientSequence,packet.sequence);
    }
    // Each author has its own sequence/timeline envelope. The payload below is
    // independent of both capture callbacks and the production UTF-16 encoder.
    const int hostFrameTick=std::max(captureBarrier+delay,previousHostTick+1);
    const int clientFrameTick=std::max(client.lockstep().currentBarrierTick()+delay,previousClientTick+1);
    std::vector<RuntimeMultiplayerGameplayCommand> capturedA,capturedB;std::vector<int> captureTicksA,captureTicksB;
    RuntimeMultiplayerPlayerIntentRouter routerA([&](const RuntimeMultiplayerGameplayCommand& value,std::string& why){
        const bool accepted=host.captureGameplayCommand(value,why);if(accepted){capturedA.push_back(value);captureTicksA.push_back(hostWorld.tick);}return accepted;});
    RuntimeMultiplayerPlayerIntentRouter routerB([&](const RuntimeMultiplayerGameplayCommand& value,std::string& why){
        const bool accepted=client.captureGameplayCommand(value,why);if(accepted){capturedB.push_back(value);captureTicksB.push_back(clientWorld.tick);}return accepted;});
    RuntimePlayerCommunicationState uiA,uiB;ChatHost a(routerA,uiA),b(routerB,uiB);
    const auto pending=[&](const ChatHost& input,std::size_t count,bool hostAuthor,const std::string& label){
        const auto pendingCount=hostAuthor?host.pendingGameplayCommandCount():client.pendingGameplayCommandCount();
        context.expect(input.accepted&&pendingCount==count&&hostWorld.tick==captureTick&&clientWorld.tick==captureTick
            &&hostWorld.applied.empty()&&clientWorld.applied.empty()&&!input.ui.chatOpen&&input.ui.chatDraft.empty(),
            label+" authors pending without advancing; accepted submission clears draft");};
    context.expect(enterText(a,u"all",false,0),"ChatAll through both controller entries");pending(a,1,true,"ChatAll");
    context.expect(enterText(a,u"\u4e2d\u6587",true,3),"ChatAllies BMP input");pending(a,2,true,"ChatAllies");
    context.expect(enterText(a,u"self",false,1),"explicit sender-slot recipient bit");pending(a,3,true,"self recipient");
    context.expect(enterText(a,std::u16string{0xd83d,0xde00},true,3),"ChatAllies supplementary pair");pending(a,4,true,"supplementary");
    context.expect(enterText(a,u"marker",false,0,true),"owned BeaconText cell");pending(a,5,true,"BeaconText");
    context.expect(enterText(a,u" \t ",false,0,true),"empty trimmed BeaconText permitted");pending(a,6,true,"empty BeaconText");
    context.expect(enterText(a,std::u16string(256,u'x'),false,0,false,{9,11},true),"prefilled exact 256 UTF-8 bytes");pending(a,7,true,"prefilled limit");
    context.expect(enterText(a,u"  trimmed  ",false,3),"ordinary trim and explicit both-slot mask");pending(a,8,true,"trimmed");
    context.expect(enterText(b,u"client-all",false,0),"client authenticated ChatAll");pending(b,1,false,"client ChatAll");
    context.expect(enterText(b,u"client allies",true,3),"client authenticated Allies");pending(b,2,false,"client Allies");
    context.expect(enterText(b,u"client-marker",false,0,true,{7,13}),"client own BeaconText");pending(b,3,false,"client BeaconText");
    context.expect(capturedA==expectedA&&capturedB==expectedB,"eleven independently authored kind/text/mask/cell payloads from both actual peers");
    const auto dispatches=a.dispatches;
    uiA.chatOpen=true;uiA.chatDraft=u"repeat";
    context.expect(handleRuntimeChatTextKey({13,true},uiA,a)&&a.dispatches==dispatches&&uiA.chatDraft==u"repeat","repeated Enter consumes without capture");
    uiA.chatDraft=u" \t ";context.expect(handleRuntimeChatTextKey({13,false},uiA,a)&&!uiA.chatOpen&&a.dispatches==dispatches,"empty ordinary chat closes without authoring");
    uiA.chatOpen=true;uiA.chatDraft=std::u16string{0xd800};
    context.expect(handleRuntimeChatTextKey({13,false},uiA,a)&&!uiA.chatOpen&&a.dispatches==dispatches,"invalid ordinary UTF-16 follows original empty conversion branch");
    uiA.chatOpen=true;uiA.beaconEditing=true;uiA.chatDraft=u"stale";a.selectedValid=false;
    context.expect(handleRuntimeChatTextKey({13,false},uiA,a)&&!uiA.chatOpen&&!uiA.selectedBeacon&&a.dispatches==dispatches,"stale beacon never authors");
    context.expect(enterText(a,u"capacity",false,0)&&!a.accepted&&uiA.chatOpen&&uiA.chatDraft==u"capacity"
        &&host.pendingGameplayCommandCount()==hostCount,"real running bridge capacity rejects while preserving original draft");
    for(int mode=0;mode<3;++mode){
        RuntimeMultiplayerPlayerIntentAuthorityState authority;authority.spectator=mode==0;authority.terminal=mode==1;authority.localDefeated=mode==2;
        routerB.setAuthorityState(authority);const auto previous=capturedB.size();
        context.expect(enterText(b,u"authority",false,0)&&!b.accepted&&uiB.chatOpen&&uiB.chatDraft==u"authority"
            &&capturedB.size()==previous&&client.pendingGameplayCommandCount()==clientCount,"real intent authority rejects unchanged text "+std::to_string(mode));
    }
    routerB.setAuthorityState({});
    context.expect(std::all_of(captureTicksA.begin(),captureTicksA.end(),[&](int tick){return tick==captureTick;})
        &&std::all_of(captureTicksB.begin(),captureTicksB.end(),[&](int tick){return tick==captureTick;}),"both peers retain original runtime capture tick");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);bool complete=false;
    while(std::chrono::steady_clock::now()<deadline){
        const auto hp=host.pump(true);const auto cp=client.pump(false);
        if(!hp.succeeded||!cp.succeeded){context.expect(false,"chat real pump: "+(!hp.succeeded?hp.error:cp.error));break;}
        complete=hostWorld.applied.size()==totalCount&&clientWorld.applied.size()==totalCount
            &&hostWorld.tick>=std::max(hostFrameTick,clientFrameTick)&&hostWorld.tick==clientWorld.tick&&observerHash(hostWorld)==observerHash(clientWorld);
        if(complete)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(complete,"both real peers consume and advance all text commands for delay "+std::to_string(delay));
    context.expect(hostWorld.applied==clientWorld.applied,"both peers apply identical authenticated envelopes");
    std::vector<RuntimeMultiplayerLockstepApplyCommand> expectedApplied;
    const auto appendExpected=[&](const auto& commands,const std::string& peer,int tick,std::uint64_t sequence){
        for(std::size_t index=0;index<commands.size();++index){
            RuntimeMultiplayerLockstepApplyCommand item;item.tick=tick;item.peerId=peer;
            item.frameSequence=sequence;item.commandIndex=index;item.command=commands[index];expectedApplied.push_back(std::move(item));
        }
    };
    appendExpected(expectedA,"host-peer",hostFrameTick,hostSequence+1);
    appendExpected(expectedB,"client-peer",clientFrameTick,clientSequence+1);
    std::sort(expectedApplied.begin(),expectedApplied.end(),[](const auto& left,const auto& right){
        if(left.tick!=right.tick)return left.tick<right.tick;
        if(left.peerId!=right.peerId)return left.peerId<right.peerId;
        return left.commandIndex<right.commandIndex;
    });
    context.expect(hostWorld.applied==expectedApplied&&clientWorld.applied==expectedApplied,
        "independent complete envelopes at every global tick/peer/index position on both real consumers");
    for(const auto& peer:std::vector<std::string>{"host-peer","client-peer"}){
        const bool hostAuthor=peer=="host-peer";const auto& expected=hostAuthor?expectedA:expectedB;
        const int frameTick=hostAuthor?hostFrameTick:clientFrameTick;const auto sequence=(hostAuthor?hostSequence:clientSequence)+1;
        std::vector<RuntimeMultiplayerCommandPacket> authored,replicated;
        for(const auto& packet:hostWorld.observedFrames)if(packet.peerId==peer&&packet.tick==frameTick)authored.push_back(packet);
        for(const auto& packet:clientWorld.observedFrames)if(packet.peerId==peer&&packet.tick==frameTick)replicated.push_back(packet);
        context.expect(authored.size()==1&&replicated.size()==1,"owned real wire copied before retirement for "+peer);
        if(authored.size()==1&&replicated.size()==1){RuntimeMultiplayerGameplayFrame frame,replica;
            context.expect(decodeRuntimeMultiplayerGameplayFrame(authored.front().payload,frame,error,hostCount)&&frame.commands==expected,"wire payload matches independent UTF-8/kind/mask/cell vectors for "+peer);
            context.expect(decodeRuntimeMultiplayerGameplayFrame(replicated.front().payload,replica,error,hostCount)&&replica==frame,"replica wire decodes independently for "+peer);
            context.expect(authored.front()==replicated.front()&&authored.front().sequence==sequence,"real peer ownership/sequence/packet equality for "+peer);
            evidence<<"chat-text bridge wire delay="<<delay<<" tick="<<authored.front().tick<<" peer="<<peer<<" sequence="<<sequence
                <<" timelineEpoch="<<authored.front().timelineEpoch<<" payload="<<hex(authored.front().payload)<<'\n';
        }
    }
    for(std::size_t index=0;index<hostWorld.applied.size();++index){const auto& item=hostWorld.applied[index];
        const bool hostAuthor=item.peerId=="host-peer";const auto& expected=hostAuthor?expectedA:expectedB;
        const int expectedTick=hostAuthor?hostFrameTick:clientFrameTick;const auto sequence=(hostAuthor?hostSequence:clientSequence)+1;
        context.expect((hostAuthor||item.peerId=="client-peer")&&item.commandIndex<expected.size()
            &&item.command==expected[item.commandIndex]&&item.tick==expectedTick&&item.frameSequence==sequence,"authenticated sender/index/sequence/payload/apply tick "+std::to_string(index));
        const bool hostPrevalidated=std::any_of(hostWorld.prevalidated.begin(),hostWorld.prevalidated.end(),[&](const auto& values){return std::find(values.begin(),values.end(),item)!=values.end();});
        const bool clientPrevalidated=std::any_of(clientWorld.prevalidated.begin(),clientWorld.prevalidated.end(),[&](const auto& values){return std::find(values.begin(),values.end(),item)!=values.end();});
        context.expect(hostPrevalidated&&clientPrevalidated,"both real prevalidation callbacks observed same command "+std::to_string(index));
        std::vector<std::uint8_t> payload;context.expect(encodeRuntimeMultiplayerGameplayCommand(item.command,payload,error),"retained canonical command encode");
        evidence<<"chat-text bridge trace delay="<<delay<<" captureTick="<<captureTick<<" captureBarrier="<<captureBarrier
            <<" frameTick="<<item.tick<<" peer="<<item.peerId<<" frameSequence="<<item.frameSequence<<" commandIndex="<<item.commandIndex
            <<" hostPrevalidated="<<hostPrevalidated<<" clientPrevalidated="<<clientPrevalidated<<" payload="<<hex(payload)<<'\n';
    }
    context.expect(hostWorld.tick==clientWorld.tick&&observerHash(hostWorld)==observerHash(clientWorld),"both observer worlds agree after real consumption");
    for(const auto* world:{&hostWorld,&clientWorld}){
        context.expect(world->order.size()%2==0,"every tick has apply and advance");
        for(std::size_t i=0;i+1<world->order.size();i+=2)context.expect(world->order[i].first=='A'&&world->order[i+1].first=='V'
            &&world->order[i].second==world->order[i+1].second,"exact apply before same-tick advance");
        const auto peer=world==&hostWorld?"host":"client";
        for(const auto& entry:world->order)evidence<<"chat-text bridge order delay="<<delay<<" peer="<<peer<<" stage="<<entry.first<<" tick="<<entry.second<<'\n';
    }
    const auto trace=evidence.str();std::cout<<trace;
    char* directory=nullptr;std::size_t length=0;const auto envResult=_dupenv_s(&directory,&length,"CONCORD_CHAT_TEXT_BRIDGE_OUTPUT");
    context.expect(envResult==0,"isolated owned environment read");
    if(directory&&*directory){std::error_code ec;std::filesystem::create_directories(directory,ec);context.expect(!ec,"isolated bridge directory");
        if(!ec){std::ofstream output(std::filesystem::path(directory)/("bridge-delay-"+std::to_string(delay)+".txt"),std::ios::binary|std::ios::trunc);
            output<<trace;output.flush();context.expect(static_cast<bool>(output),"complete real wire/consume evidence retained");}}
    std::free(directory);host.detachRuntime();client.detachRuntime();host.lan().close();client.lan().close();
    (void)consumeRuntimeMultiplayerVisibleFailure();return context.failures==beforeFailures;
}
} // namespace
int main(){SmokeContext context;(void)runDelay(context,0);(void)runDelay(context,2);if(context.failures)return 1;
    std::cout<<"Chat-text bridge passed: checks="<<context.checks
        <<" delays=0,2 commands=11 hostCommands=8 clientCommands=3 chatAll=5 chatAllies=3 beaconText=3"
        <<" capacityReject=1 routerAuthorityReject=3 repeat=1 empty=1 malformed=1 staleBeacon=1"
        <<" sameProductionCpp=1 realLoopbackBridge=1 actualWireBeforeRetirement=1 prevalidateApplyAdvance=1"
        <<" recipientMasks=owned-port-only shellWorld=not-covered fullSnapshot=not-run externalNetwork=not-run\n";return 0;}
