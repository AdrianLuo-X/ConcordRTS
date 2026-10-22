#include "ui/input/RuntimeReferenceCommunicationController.h"
#include "ui/input/RuntimePlayerCommands.h"
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
        if (!condition) { ++failures; std::cerr << "FAIL reference-communication bridge: " << message << '\n'; }
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
    callbacks.stageVerifiedSnapshot = [](const RuntimeMultiplayerSnapshotDescriptor&, std::span<const std::uint8_t>, std::uint64_t&, std::string& error) { error="snapshot not exercised by reference-communication bridge fixture"; return false; };
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


struct CommunicationHost final : RuntimeReferenceCommunicationHost {
    RuntimeMultiplayerPlayerIntentRouter& router;
    RuntimePlayerCommunicationState& ui;
    std::size_t dispatches=0;
    bool accepted=false;
    std::string error;
    CommunicationHost(RuntimeMultiplayerPlayerIntentRouter& value,RuntimePlayerCommunicationState& presentation)
        :router(value),ui(presentation){}
    std::string currentOwner() override {return "skirmishplayer1";}
    std::string canonicalOwner(const std::string& owner) override {return owner=="Player"?"skirmishplayer1":owner;}
    std::optional<bool> allianceOffer(const std::string&,const std::string&) override {return false;}
    bool ownersAllied(const std::string&,const std::string&) override {return false;}
    void dispatchCommunication(RuntimeMultiplayerGameplayPlayerCommunicationCommand command) override {
        ++dispatches;accepted=router.communicate(std::move(command),error);
    }
    std::int32_t binding(const std::string&,std::int32_t fallback) override {return fallback;}
    std::optional<std::string> objectOwner(std::int32_t) override {return std::string("skirmishplayer2");}
    void openDiplomacy() override {ui.diplomacyOpen=true;ui.diplomacyRow=1;}
    std::optional<RuntimePlayerBeacon> selectedBeacon() override {
        return RuntimePlayerBeacon{"skirmishplayer1",{9,11},-1,"bridge beacon",0};
    }
    void setChatDraftUtf8(const std::string& value) override {ui.chatDraft.assign(value.begin(),value.end());}
    std::string viewerOwner() override {return currentOwner();}
    bool beaconVisible(const RuntimePlayerBeacon&) override {return true;}
    void cancelSuperWeapon(const std::string&) override {}
    void dismissPlacement() override {}
    void setSidebarTool(RuntimeSidebarTool) override {}
    void setSelectionDragMoved(bool) override {}
    void setSelectionDragActive(bool) override {}
    void beaconLimitStatus() override {}
    void dispatchWaypoint(RuntimePlayerWaypointOperation operation,std::uint64_t id) override {
        ++dispatches;accepted=router.planWaypoints({operation,id},error);
    }
};
struct DiplomacyHost final : RuntimeDiplomacyNavigationHost {
    CommunicationHost& communication;
    explicit DiplomacyHost(CommunicationHost& value):communication(value){}
    std::string currentOwner() override {return communication.currentOwner();}
    std::string canonicalOwner(const std::string& value) override {return communication.canonicalOwner(value);}
    std::optional<bool> allianceOffer(const std::string& a,const std::string& b) override {return communication.allianceOffer(a,b);}
    bool ownersAllied(const std::string& a,const std::string& b) override {return communication.ownersAllied(a,b);}
    void dispatchCommunication(RuntimeMultiplayerGameplayPlayerCommunicationCommand command) override {communication.dispatchCommunication(std::move(command));}
    std::vector<RuntimeCommunicationDiplomacyRow> rows() override {
        return {{"skirmishplayer1",true,false},{"skirmishplayer2",false,false}};
    }
    std::int32_t binding(const std::string&,std::int32_t fallback) override {return fallback;}
    void closePage() override {communication.ui.diplomacyOpen=false;}
    bool allianceEnabled(const RuntimeCommunicationDiplomacyRow&) override {return true;}
    void requestRepaint() override {}
};
std::vector<RuntimeMultiplayerGameplayCommand> expectedCommands() {
    std::vector<RuntimeMultiplayerGameplayCommand> result;
    const auto add=[&](RuntimePlayerCommunicationKind kind,std::uint8_t value=0) {
        RuntimeMultiplayerGameplayPlayerCommunicationCommand body;body.kind=kind;body.value=value;
        if(kind==RuntimePlayerCommunicationKind::Alliance)body.text="skirmishplayer2";
        if(kind==RuntimePlayerCommunicationKind::DeleteBeacon)body.cell={9,11};
        RuntimeMultiplayerGameplayCommand command;command.body=std::move(body);result.push_back(std::move(command));
    };
    add(RuntimePlayerCommunicationKind::Alliance,1);add(RuntimePlayerCommunicationKind::DeleteBeacon);add(RuntimePlayerCommunicationKind::Cheer);
    for(std::uint8_t i=1;i<=8;++i)add(RuntimePlayerCommunicationKind::Taunt,i);
    RuntimeMultiplayerGameplayCommand waypoint;waypoint.body=RuntimeMultiplayerGameplayWaypointCommand{RuntimePlayerWaypointOperation::DeletePoint,73};result.push_back(std::move(waypoint));
    add(RuntimePlayerCommunicationKind::Alliance,1);return result;
}
bool runDelay(SmokeContext& context,int delay) {
    const int beforeFailures=context.failures;constexpr std::size_t commandCount=13;std::ostringstream evidence;
    RuntimeMultiplayerLockstepConfig config;config.inputDelayTicks=delay;config.maximumCommandsPerFrame=commandCount;
    RuntimeMultiplayerRuntimeBridge host(config),client(config);
    if(!startRunningPair(host,client,context,0x5730303905000000ULL+static_cast<std::uint64_t>(delay),"reference communication delay "+std::to_string(delay)))return false;
    ObservedWorld hostWorld,clientWorld;std::string error;
    context.expect(host.attachRuntime(callbacksFor(hostWorld,host),error),"communication host attach: "+error);
    error.clear();context.expect(client.attachRuntime(callbacksFor(clientWorld,client),error),"communication client attach: "+error);
    if(context.failures!=beforeFailures)return false;
    if(!pumpUntil(host,client,[&](){return host.initialStateHashVerified()&&client.initialStateHashVerified();},context,"communication initial hash"))return false;
    const int captureTick=hostWorld.tick,captureBarrier=host.lockstep().currentBarrierTick();
    int lastAuthoredTick=-1;std::uint64_t lastSequence=0;
    for(const auto& packet:host.lan().session().commands())if(packet.peerId=="host-peer"){
        lastAuthoredTick=std::max(lastAuthoredTick,packet.tick);lastSequence=std::max(lastSequence,packet.sequence);
    }
    const int expectedFrameTick=std::max(captureBarrier+delay,lastAuthoredTick+1);
    std::vector<RuntimeMultiplayerGameplayCommand> captured;std::vector<int> capturedAt;
    RuntimeMultiplayerPlayerIntentRouter router([&](const RuntimeMultiplayerGameplayCommand& command,std::string& why){
        const bool accepted=host.captureGameplayCommand(command,why);if(accepted){captured.push_back(command);capturedAt.push_back(hostWorld.tick);}return accepted;
    });
    RuntimePlayerLocalCommands local;RuntimePlayerCommunicationState ui;CommunicationHost inputHost(router,ui);DiplomacyHost diplomacy(inputHost);const std::vector<int> selection{9};
    const auto key=[&](std::int32_t code,bool repeated=false){return handleRuntimeReferenceCommunicationKey({code,repeated},local,ui,selection,inputHost);};
    const auto pending=[&](std::size_t count,const std::string& label){context.expect(inputHost.accepted&&host.pendingGameplayCommandCount()==count
        &&hostWorld.tick==captureTick&&hostWorld.applied.empty(),label+" captures only pending at original tick");};
    context.expect(key('A'),"same production Reference A");pending(1,"Alliance");
    context.expect(key(46),"same production Delete binding");pending(2,"DeleteBeacon");
    context.expect(key('C'),"same production Cheer");pending(3,"Cheer");
    for(int i=1;i<=8;++i){context.expect(key(116+i-1),"same production Taunt "+std::to_string(i));pending(static_cast<std::size_t>(i+3),"Taunt");}
    local.planning=true;local.selectedWaypoint=73;context.expect(key(46),"same production planning Delete priority");pending(12,"DeletePoint");
    ui.diplomacyOpen=true;ui.diplomacyRow=1;context.expect(handleRuntimeDiplomacyNavigationKey({32,false},ui,diplomacy),"same production modal Alliance");pending(13,"ModalAlliance");
    const auto expected=expectedCommands();context.expect(captured==expected,"canonical payloads independently match all13 explicit ownerless expected commands");
    const auto dispatches=inputHost.dispatches;
    context.expect(key('A',true)&&inputHost.dispatches==dispatches&&host.pendingGameplayCommandCount()==commandCount,"repeat consumes without authoring");
    context.expect(handleRuntimeDiplomacyNavigationKey({32,true},ui,diplomacy)&&inputHost.dispatches==dispatches,"modal repeat never authors");
    toggleRuntimeCommunicationAlliance({},inputHost);context.expect(inputHost.dispatches==dispatches,"empty shared toggle never authors");
    context.expect(key('C')&&!inputHost.accepted&&host.pendingGameplayCommandCount()==commandCount,"real bridge capacity rejects extra consumed Cheer");
    for(int mode=0;mode<3;++mode){RuntimeMultiplayerPlayerIntentAuthorityState authority;authority.spectator=mode==0;authority.terminal=mode==1;authority.localDefeated=mode==2;
        router.setAuthorityState(authority);const auto count=captured.size();context.expect(key('C')&&!inputHost.accepted&&captured.size()==count&&host.pendingGameplayCommandCount()==commandCount,"real intent authority rejects "+std::to_string(mode));}
    router.setAuthorityState({});context.expect(captured.size()==commandCount&&std::all_of(capturedAt.begin(),capturedAt.end(),[&](int tick){return tick==captureTick;}),"all captures retain original runtime tick");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);bool complete=false;
    while(std::chrono::steady_clock::now()<deadline){const auto hp=host.pump(true);const auto cp=client.pump(false);
        if(!hp.succeeded||!cp.succeeded){context.expect(false,"communication real pump: "+(!hp.succeeded?hp.error:cp.error));break;}
        complete=hostWorld.applied.size()==commandCount&&clientWorld.applied.size()==commandCount&&hostWorld.tick>=expectedFrameTick
            &&hostWorld.tick==clientWorld.tick&&observerHash(hostWorld)==observerHash(clientWorld);if(complete)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    context.expect(complete,"communication actual frame apply/advance delay "+std::to_string(delay));context.expect(hostWorld.applied==clientWorld.applied,"both peers apply identical authenticated envelopes");
    std::vector<RuntimeMultiplayerCommandPacket> authored,replicated;
    for(const auto& packet:hostWorld.observedFrames)if(packet.peerId=="host-peer"&&packet.tick==expectedFrameTick)authored.push_back(packet);
    for(const auto& packet:clientWorld.observedFrames)if(packet.peerId=="host-peer"&&packet.tick==expectedFrameTick)replicated.push_back(packet);
    context.expect(authored.size()==1&&replicated.size()==1,"actual host/client wire copied before queue retirement");
    if(authored.size()==1&&replicated.size()==1){RuntimeMultiplayerGameplayFrame frame,replica;
        context.expect(decodeRuntimeMultiplayerGameplayFrame(authored.front().payload,frame,error,commandCount)&&frame.commands==expected,"actual host wire equals independently authored expected payloads");
        context.expect(decodeRuntimeMultiplayerGameplayFrame(replicated.front().payload,replica,error,commandCount)&&replica==frame,"independent client wire decodes every original payload");
        context.expect(authored.front()==replicated.front()&&authored.front().sequence==lastSequence+1,"actual wire ownership/sequence/envelope equality");
        std::ostringstream wire;for(auto byte:authored.front().payload)wire<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(byte);
        evidence<<"reference-communication bridge wire delay="<<delay<<" tick="<<authored.front().tick<<" peer="<<authored.front().peerId<<" sequence="<<authored.front().sequence<<" timelineEpoch="<<authored.front().timelineEpoch<<" payload="<<wire.str()<<'\n';}
    for(std::size_t index=0;index<hostWorld.applied.size();++index){const auto& item=hostWorld.applied[index];
        context.expect(index<expected.size()&&item.command==expected[index]&&item.peerId=="host-peer"&&item.tick==expectedFrameTick&&item.frameSequence==lastSequence+1&&item.commandIndex==index,"authority owner/payload/order/tick/sequence/index "+std::to_string(index));
        const bool prevalidated=std::any_of(hostWorld.prevalidated.begin(),hostWorld.prevalidated.end(),[&](const auto& batch){return std::find(batch.begin(),batch.end(),item)!=batch.end();});
        const bool clientPrevalidated=std::any_of(clientWorld.prevalidated.begin(),clientWorld.prevalidated.end(),[&](const auto& batch){return std::find(batch.begin(),batch.end(),item)!=batch.end();});
        context.expect(prevalidated&&clientPrevalidated,"both real prevalidation ports observed identical consumed command "+std::to_string(index));
        std::vector<std::uint8_t> payload;encodeRuntimeMultiplayerGameplayCommand(item.command,payload,error);std::ostringstream hex;for(auto byte:payload)hex<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(byte);
        evidence<<"reference-communication bridge trace delay="<<delay<<" captureTick="<<captureTick<<" captureBarrier="<<captureBarrier<<" frameTick="<<item.tick<<" peer="<<item.peerId<<" frameSequence="<<item.frameSequence<<" commandIndex="<<item.commandIndex<<" hostPrevalidated="<<prevalidated<<" clientPrevalidated="<<clientPrevalidated<<" payload="<<hex.str()<<'\n';}
    context.expect(hostWorld.tick==clientWorld.tick&&observerHash(hostWorld)==observerHash(clientWorld),"both bridge observer worlds agree after consumption");context.expect(hostWorld.order.size()%2==0&&clientWorld.order.size()%2==0,"every commit has apply then fixed advance");
    for(const auto& entry:hostWorld.order)evidence<<"reference-communication bridge order delay="<<delay<<" peer=host stage="<<entry.first<<" tick="<<entry.second<<'\n';
    for(const auto& entry:clientWorld.order)evidence<<"reference-communication bridge order delay="<<delay<<" peer=client stage="<<entry.first<<" tick="<<entry.second<<'\n';
    const std::string trace=evidence.str();std::cout<<trace;
    char* directory=nullptr;std::size_t directoryLength=0;
    const auto environmentResult=_dupenv_s(&directory,&directoryLength,"CONCORD_REFERENCE_COMMUNICATION_BRIDGE_OUTPUT");
    context.expect(environmentResult==0,"isolated bridge evidence environment");
    if(directory&&*directory){
        std::error_code ec;std::filesystem::create_directories(directory,ec);
        context.expect(!ec,"isolated bridge evidence directory");
        if(!ec){std::ofstream file(std::filesystem::path(directory)/("bridge-delay-"+std::to_string(delay)+".txt"),std::ios::binary|std::ios::trunc);
            file<<trace;file.flush();context.expect(static_cast<bool>(file),"retained actual wire/prevalidation/consumption evidence");}
    }
    std::free(directory);
    host.detachRuntime();client.detachRuntime();host.lan().close();client.lan().close();(void)consumeRuntimeMultiplayerVisibleFailure();return context.failures==beforeFailures;
}
} // namespace
int main(){SmokeContext context;(void)runDelay(context,0);(void)runDelay(context,2);if(context.failures)return 1;
    std::cout<<"Reference-communication bridge passed: checks="<<context.checks
        <<" delays=0,2 commands=13 referenceActions=12 modalAlliance=1 repeat=1 emptyToggle=1 bridgeCapacityReject=1 routerAuthorityReject=3"
        <<" sameProductionCpp=1 realLoopbackBridge=1 actualWireBeforeRetirement=1 prevalidateApplyAdvance=1"
        <<" shellWorld=not-covered fullSnapshot=not-run externalNetwork=not-run\n";return 0;}