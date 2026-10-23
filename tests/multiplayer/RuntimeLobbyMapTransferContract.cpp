#include "network/lobby/RuntimeMultiplayerLobbyMapTransfer.h"
#include "network/maps/RuntimeMultiplayerCompatibilityFingerprint.h"

#include <iostream>
#include <stdexcept>

namespace {
using Transfer = RuntimeMultiplayerLobbyMapTransfer;
using Type = RuntimeMultiplayerMessageType;
int checks = 0;
void expect(bool value, const char* reason) { ++checks; if (!value) throw std::runtime_error(reason); }
template<class T> RuntimeMultiplayerMessage message(Type type, T body) {
    RuntimeMultiplayerMessage value; value.type = type; value.body = std::move(body); return value;
}
struct Fixture {
    Transfer host, client;
    Transfer::HostPeer peer;
    Transfer::TimePoint now{};
    std::vector<std::uint8_t> data;
    RuntimeMultiplayerMatchMap map;
    RuntimeMultiplayerMapRequest request;
    Transfer::Input input{7, true, true, false, &map};
    Transfer::Input clientInput{0, true, true, false, &map};
    Fixture() : data(runtimeMultiplayerSnapshotChunkBytes * 2 + 19, 0x63) {
        map.canonicalId = "multimd/contract.map";
        std::string error;
        expect(runtimeMultiplayerContentFingerprint({{map.canonicalId, data}}, map.contentFingerprint, error), "fixture fingerprint");
        expect(host.publish(map, data, error), "verified publication");
        expect(client.prepareRequest(map, request, error) && request.requestId == 1, "request starts at1");
        client.requested(request, now);
        const auto accepted = host.receiveHost(peer, input, message(Type::LobbyMapRequest, request), now);
        expect(accepted.handled && accepted.failure == Transfer::Failure::None && accepted.peerId == 7 && accepted.transferId == 1, "explicit peer and transfer binding");
    }
    Transfer::Action offer() {
        auto action = host.pollHost(peer, input, now);
        expect(action.send && action.send->type == Type::LobbyMapOffer, "offer precedes first chunk");
        host.sentHost(peer, action, now);
        const auto accepted = client.receiveClient(clientInput, *action.send);
        expect(accepted.failure == Transfer::Failure::None, "client accepts offer");
        return action;
    }
    Transfer::Action chunk() {
        auto action = host.pollHost(peer, input, now);
        expect(action.send && action.send->type == Type::LobbyMapChunk, "one current chunk");
        host.sentHost(peer, action, now); return action;
    }
};
void transfers() {
    Fixture f;
    f.offer(); auto first = f.chunk();
    expect(!f.host.hostComplete(f.peer, &f.map), "start gate remains closed");
    expect(!f.host.pollHost(f.peer, f.input, f.now + std::chrono::milliseconds(249)).send, "no retry at249ms");
    auto retry = f.host.pollHost(f.peer, f.input, f.now + std::chrono::milliseconds(250));
    expect(retry.send == first.send, "exact original bytes retransmitted at250ms");
    auto ack = f.client.receiveClient(f.clientInput, *first.send);
    expect(ack.send && ack.send->type == Type::LobbyMapAck && !ack.complete && f.client.bytes().empty(), "partial bytes hidden until verified");
    expect(f.client.receiveClient(f.clientInput, *first.send).send == ack.send, "duplicate chunk idempotent");
    expect(f.host.receiveHost(f.peer, f.input, *ack.send, f.now).failure == Transfer::Failure::None, "exact ack accepted");
    expect(f.host.receiveHost(f.peer, f.input, *ack.send, f.now).failure == Transfer::Failure::None, "duplicate ack accepted");
    for (int index = 1; index < 3; ++index) {
        const auto chunk = f.chunk();
        ack = f.client.receiveClient(f.clientInput, *chunk.send);
        expect(ack.send.has_value(), "chunk produces ack");
        const auto completion = f.host.receiveHost(f.peer, f.input, *ack.send, f.now);
        expect(completion.complete == (index == 2), "host completion only on final ack");
        if (index == 2) {
            expect(ack.complete && f.client.bytes() == f.data, "complete verified image published");
            expect(f.client.receiveClient(f.clientInput, *chunk.send).send == ack.send, "completed duplicate resends exact ack");
            auto altered = *chunk.send; std::get<RuntimeMultiplayerSnapshotChunk>(altered.body).bytes[0] ^= 1;
            expect(f.client.receiveClient(f.clientInput, altered).reason == "completed map received conflicting bytes", "completed conflict rejected");
        }
    }
    expect(f.host.hostComplete(f.peer, &f.map) && !f.host.pollHost(f.peer, f.input, f.now).send, "complete host is quiescent");
    f.client.reset();
    expect(f.client.bytes().empty() && !f.client.offer(), "disconnect/reset releases image and offer");
    RuntimeMultiplayerMapRequest next; std::string error;
    expect(f.client.prepareRequest(f.map, next, error) && next.requestId == 1, "new session resets sequence");
}
void failures() {
    for (int mode = 0; mode < 15; ++mode) {
        Fixture f; Transfer::Action action;
        if (mode == 0) { RuntimeMultiplayerReadyMessage ready; ready.ready = true; action = f.host.receiveHost(f.peer, f.input, message(Type::Ready, ready), f.now); }
        if (mode == 1) { auto altered = f.request; altered.map.contentFingerprint ^= 1; action = f.host.receiveHost(f.peer, f.input, message(Type::LobbyMapRequest, altered), f.now); }
        if (mode == 2) { RuntimeMultiplayerSnapshotChunkAck ack{1, 1, 0}; action = f.host.receiveHost(f.peer, f.input, message(Type::LobbyMapAck, ack), f.now); }
        if (mode == 3) { action = f.host.pollHost(f.peer, f.input, f.now + std::chrono::seconds(180)); expect(action.failure == Transfer::Failure::Transport, "host timeout preserves transport category"); }
        if (mode == 4) { expect(f.client.pollClient(f.clientInput, f.now + std::chrono::seconds(180) - std::chrono::milliseconds(1)).failure == Transfer::Failure::None, "before deadline accepted"); action = f.client.pollClient(f.clientInput, f.now + std::chrono::seconds(180)); }
        if (mode == 5) { f.offer(); auto chunk = f.chunk(); std::get<RuntimeMultiplayerSnapshotChunk>(chunk.send->body).chunkIndex = 2; action = f.client.receiveClient(f.clientInput, *chunk.send); }
        if (mode == 6) { f.offer(); auto chunk = f.chunk(); f.client.receiveClient(f.clientInput, *chunk.send); std::get<RuntimeMultiplayerSnapshotChunk>(chunk.send->body).bytes[0] ^= 1; action = f.client.receiveClient(f.clientInput, *chunk.send); }
        if (mode == 7) { f.offer(); auto chunk = f.chunk(); std::get<RuntimeMultiplayerSnapshotChunk>(chunk.send->body).bytes.resize(runtimeMultiplayerSnapshotChunkBytes + 1); action = f.client.receiveClient(f.clientInput, *chunk.send); }
        if (mode == 8) { auto offer = f.host.pollHost(f.peer, f.input, f.now); std::get<RuntimeMultiplayerMapOffer>(offer.send->body).transfer.byteCount = runtimeMultiplayerSnapshotDefaultMaximumBytes + 1; action = f.client.receiveClient(f.clientInput, *offer.send); }
        if (mode == 9) { action = f.client.receiveClient(f.clientInput, message(Type::LobbyMapRequest, f.request)); }
        if (mode == 10) { RuntimeMultiplayerSnapshotChunk chunk; chunk.proposalId = 1; chunk.sourceTimelineEpoch = 1; chunk.bytes = {1}; action = f.client.receiveClient(f.clientInput, message(Type::LobbyMapChunk, chunk)); }
        if (mode == 11) { auto offer = f.offer(); action = f.host.receiveHost(f.peer, f.input, *offer.send, f.now); }
        if (mode == 12) { Transfer::HostPeer other; f.input.admitted = false; action = f.host.receiveHost(other, f.input, message(Type::LobbyMapRequest, f.request), f.now); }
        if (mode == 13) { Transfer::HostPeer other; f.input.ready = true; action = f.host.receiveHost(other, f.input, message(Type::LobbyMapRequest, f.request), f.now); }
        if (mode == 14) { Transfer::HostPeer other; f.input.lobby = false; action = f.host.receiveHost(other, f.input, message(Type::LobbyMapRequest, f.request), f.now); }
        expect(action.handled && action.failure != Transfer::Failure::None && !action.reason.empty(), "invalid transition fails explicitly");
        expect(f.client.bytes().empty(), "failure publishes no partial image");
    }
}
void identityAndCleanup() {
    Fixture f; auto offer = f.offer(); auto first = f.chunk();
    auto older = first; std::get<RuntimeMultiplayerSnapshotChunk>(older.send->body).proposalId = 0;
    expect(!f.client.receiveClient(f.clientInput, *older.send).send, "stale chunk ignored");
    f.client.receiveClient(f.clientInput, *first.send);
    f.map.contentFingerprint ^= 1;
    expect(!f.host.pollHost(f.peer, f.input, f.now).send && f.host.hostComplete(f.peer, &f.map), "stale host request no longer blocks current map");
    f.client.pollClient(f.clientInput, f.now);
    expect(!f.client.offer() && f.client.bytes().empty(), "map switch discards partial assembler");
    expect(f.client.receiveClient(f.clientInput, *offer.send).failure == Transfer::Failure::None, "old offer ignored after cancelled request");
    f.host.reset(); f.peer = Transfer::HostPeer{};
    expect(f.host.hostComplete(f.peer, &f.map), "peer teardown resets start gate");
    RuntimeMultiplayerMapRequest sentinel{991, f.map}; std::string error;
    auto invalid = f.map; invalid.canonicalId = "../escape.map";
    expect(!f.client.prepareRequest(invalid, sentinel, error) && sentinel.requestId == 991, "invalid request is transactional");
    Fixture peers; Transfer::HostPeer second;
    auto secondInput = peers.input; secondInput.peerId = 9;
    peers.host.receiveHost(second, secondInput, message(Type::LobbyMapRequest, peers.request), peers.now);
    auto secondOffer = peers.host.pollHost(second, secondInput, peers.now);
    expect(secondOffer.peerId == 9 && secondOffer.transferId == 1 && secondOffer.send, "peer states stay independently addressed");
    peers.host.sentHost(second, secondOffer, peers.now);
    expect(peers.host.pollHost(peers.peer, peers.input, peers.now).send->type == Type::LobbyMapOffer, "other peer still needs its own offer");
}
}
int main() {
    try { transfers(); failures(); identityAndCleanup(); std::cout << "Lobby map transfer independent contract passed; checks=" << checks << '\n'; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
