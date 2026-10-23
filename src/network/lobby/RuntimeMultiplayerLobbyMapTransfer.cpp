#include "network/lobby/RuntimeMultiplayerLobbyMapTransfer.h"

#include <limits>
#include <utility>

namespace {
using Transfer = RuntimeMultiplayerLobbyMapTransfer;
bool matches(const RuntimeMultiplayerMatchMap* current, const RuntimeMultiplayerMatchMap& map) {
    return current && *current == map;
}
Transfer::Action consumed(const Transfer::Input& input, std::uint64_t id = 0) {
    Transfer::Action action; action.handled = true; action.peerId = input.peerId; action.transferId = id; return action;
}
Transfer::Action failed(const Transfer::Input& input, std::uint64_t id, Transfer::Failure kind, std::string reason) {
    auto action = consumed(input, id); action.failure = kind; action.reason = std::move(reason); return action;
}
Transfer::Action send(const Transfer::Input& input, std::uint64_t id, RuntimeMultiplayerMessageType type,
    RuntimeMultiplayerMessageBody body) {
    auto action = consumed(input, id); action.send.emplace(); action.send->type = type; action.send->body = std::move(body); return action;
}
}

void RuntimeMultiplayerLobbyMapTransfer::reset() {
    hostedMap_.reset(); hostedBytes_.clear(); requested_.reset(); offer_.reset();
    bytes_.clear(); assembler_.reset(); lastChunk_.reset(); nextRequestId_ = 1; requestedAt_ = {};
}

bool RuntimeMultiplayerLobbyMapTransfer::publish(RuntimeMultiplayerMatchMap map,
    std::vector<std::uint8_t> bytes, std::string& error) {
    if (hostedMap_ && *hostedMap_ == map && hostedBytes_ == bytes) { error.clear(); return true; }
    if (!validateRuntimeMultiplayerMapBytes(map, bytes, error)) return false;
    hostedMap_ = std::move(map); hostedBytes_ = std::move(bytes); error.clear(); return true;
}

bool RuntimeMultiplayerLobbyMapTransfer::hostComplete(const HostPeer& peer,
    const RuntimeMultiplayerMatchMap* currentMap) const {
    return !peer.request_ || !matches(currentMap, peer.request_->map) || peer.producer_.complete();
}

RuntimeMultiplayerLobbyMapTransfer::Action RuntimeMultiplayerLobbyMapTransfer::receiveHost(
    HostPeer& peer, const Input& input, const RuntimeMultiplayerMessage& message, TimePoint now) {
    const auto id = peer.request_ ? peer.request_->requestId : 0;
    const auto reject = [&](std::string reason) { return failed(input, id, Failure::Protocol, std::move(reason)); };
    if (message.type == RuntimeMultiplayerMessageType::Ready && peer.request_
        && matches(input.currentMap, peer.request_->map) && !peer.producer_.complete()
        && std::get<RuntimeMultiplayerReadyMessage>(message.body).ready)
        return reject("ready arrived before requested map transfer completed");
    if (message.type == RuntimeMultiplayerMessageType::LobbyMapRequest) {
        const auto& request = std::get<RuntimeMultiplayerMapRequest>(message.body);
        if (!input.lobby || !input.admitted) return reject("map request arrived outside an admitted lobby");
        if (peer.request_ && request.requestId <= peer.request_->requestId) {
            if (request.requestId == peer.request_->requestId && request != *peer.request_)
                return reject("map request sequence was reused with different content");
            return consumed(input, request.requestId);
        }
        if (!matches(input.currentMap, request.map)) return consumed(input, request.requestId);
        if (input.ready) return reject("map request requires the participant to leave Ready first");
        peer.request_ = request; peer.producer_.reset(); peer.offerSent_ = false; peer.chunkSent_ = false;
        peer.startedAt_ = now;
        return consumed(input, request.requestId);
    }
    if (message.type == RuntimeMultiplayerMessageType::LobbyMapAck) {
        const auto& ack = std::get<RuntimeMultiplayerSnapshotChunkAck>(message.body);
        if (peer.request_ && !matches(input.currentMap, peer.request_->map)) return consumed(input, ack.proposalId);
        if (peer.request_ && ack.proposalId < peer.request_->requestId) return consumed(input, ack.proposalId);
        if (!peer.request_ || !peer.offerSent_ || !input.admitted) return reject("map acknowledgement arrived without an offer");
        const auto result = peer.producer_.acknowledge(ack);
        if (!result.accepted) return reject(result.reason);
        if (result.changed) peer.chunkSent_ = false;
        auto action = consumed(input, ack.proposalId); action.complete = result.complete; return action;
    }
    if (message.type == RuntimeMultiplayerMessageType::LobbyMapOffer
        || message.type == RuntimeMultiplayerMessageType::LobbyMapChunk)
        return reject("client sent a host-only map transfer message");
    return {};
}

RuntimeMultiplayerLobbyMapTransfer::Action RuntimeMultiplayerLobbyMapTransfer::pollHost(
    HostPeer& peer, const Input& input, TimePoint now) {
    if (!input.lobby || !input.admitted || !peer.request_ || peer.producer_.complete()) return {};
    if (!matches(input.currentMap, peer.request_->map)) {
        peer.producer_.reset(); peer.offerSent_ = false; peer.chunkSent_ = false; return {};
    }
    const auto id = peer.request_->requestId;
    if (now - peer.startedAt_ >= std::chrono::seconds(180))
        return failed(input, id, Failure::Transport, "requested map transfer timed out");
    if (!hostedMap_ || *hostedMap_ != peer.request_->map || hostedBytes_.empty()) return {};
    std::string error;
    if (!peer.offerSent_) {
        RuntimeMultiplayerMapOffer offer;
        if (!makeRuntimeMultiplayerMapOffer(*peer.request_, hostedBytes_, offer, error))
            return failed(input, id, Failure::Transport, error);
        const auto begun = peer.producer_.begin(offer.transfer, hostedBytes_);
        if (!begun.accepted) return failed(input, id, Failure::Transport, begun.reason);
        return send(input, id, RuntimeMultiplayerMessageType::LobbyMapOffer, std::move(offer));
    }
    if (peer.chunkSent_ && now - peer.chunkSentAt_ < std::chrono::milliseconds(250)) return {};
    RuntimeMultiplayerSnapshotChunk chunk;
    if (!peer.producer_.currentChunk(chunk, error)) return failed(input, id, Failure::Transport, error);
    return send(input, id, RuntimeMultiplayerMessageType::LobbyMapChunk, std::move(chunk));
}

void RuntimeMultiplayerLobbyMapTransfer::sentHost(HostPeer& peer, const Action& action, TimePoint now) {
    if (!action.send) return;
    if (action.send->type == RuntimeMultiplayerMessageType::LobbyMapOffer) peer.offerSent_ = true;
    else if (action.send->type == RuntimeMultiplayerMessageType::LobbyMapChunk) {
        peer.chunkSent_ = true; peer.chunkSentAt_ = now;
    }
}

bool RuntimeMultiplayerLobbyMapTransfer::alreadyRequested(const RuntimeMultiplayerMatchMap& map) const {
    return requested_ && requested_->map == map;
}
bool RuntimeMultiplayerLobbyMapTransfer::clientIncomplete(const RuntimeMultiplayerMatchMap* currentMap) const {
    return requested_ && matches(currentMap, requested_->map) && bytes_.empty();
}
bool RuntimeMultiplayerLobbyMapTransfer::prepareRequest(const RuntimeMultiplayerMatchMap& map,
    RuntimeMultiplayerMapRequest& output, std::string& error) const {
    if (nextRequestId_ == std::numeric_limits<std::uint64_t>::max()) { error = "map request sequence exhausted"; return false; }
    RuntimeMultiplayerMapRequest request{nextRequestId_, map};
    if (!validateRuntimeMultiplayerMapRequest(request, error)) return false;
    output = std::move(request); return true;
}
void RuntimeMultiplayerLobbyMapTransfer::requested(RuntimeMultiplayerMapRequest request, TimePoint now) {
    requested_ = std::move(request); ++nextRequestId_;
    offer_.reset(); bytes_.clear(); assembler_.reset(); lastChunk_.reset(); requestedAt_ = now;
}

RuntimeMultiplayerLobbyMapTransfer::Action RuntimeMultiplayerLobbyMapTransfer::receiveClient(
    const Input& input, const RuntimeMultiplayerMessage& message) {
    const auto id = requested_ ? requested_->requestId : 0;
    const auto reject = [&](std::string reason) { return failed(input, id, Failure::Protocol, std::move(reason)); };
    if (message.type == RuntimeMultiplayerMessageType::Start && requested_
        && matches(input.currentMap, requested_->map) && bytes_.empty())
        return reject("host started before the requested map was complete");
    if (message.type == RuntimeMultiplayerMessageType::LobbyMapRequest
        || message.type == RuntimeMultiplayerMessageType::LobbyMapAck)
        return reject("host sent a client-only map transfer message");
    if (message.type == RuntimeMultiplayerMessageType::LobbyMapOffer) {
        const auto& offer = std::get<RuntimeMultiplayerMapOffer>(message.body);
        if (!requested_ && offer.request.requestId < nextRequestId_) return consumed(input, offer.request.requestId);
        if (requested_ && offer.request.requestId < requested_->requestId) return consumed(input, offer.request.requestId);
        if (!input.lobby || !input.admitted || !requested_ || offer.request != *requested_
            || (offer_ && offer != *offer_)) return reject("unsolicited or changed map transfer offer");
        if (!matches(input.currentMap, offer.request.map) || offer_) return consumed(input, offer.request.requestId);
        const auto begun = assembler_.begin(offer.transfer);
        if (!begun.accepted) return reject(begun.reason);
        offer_ = offer; return consumed(input, offer.request.requestId);
    }
    if (message.type != RuntimeMultiplayerMessageType::LobbyMapChunk) return {};
    const auto& chunk = std::get<RuntimeMultiplayerSnapshotChunk>(message.body);
    if (!requested_ && chunk.proposalId < nextRequestId_) return consumed(input, chunk.proposalId);
    if (requested_ && (chunk.proposalId < requested_->requestId || !matches(input.currentMap, requested_->map)))
        return consumed(input, chunk.proposalId);
    RuntimeMultiplayerSnapshotChunkAck ack;
    std::string error;
    if (lastChunk_) {
        if (chunk != *lastChunk_) return reject("completed map received conflicting bytes");
        ack = lastAck_;
    } else {
        if (!input.lobby || !offer_ || !assembler_.active()) return reject("map chunk arrived without an admitted offer");
        const auto result = assembler_.acceptChunk(chunk);
        if (!result.accepted || !assembler_.latestAcknowledgement(ack, error))
            return reject(result.reason.empty() ? error : result.reason);
        if (result.complete) {
            std::vector<std::uint8_t> verified;
            if (!assembler_.takeCompletedBytes(verified, error)
                || !validateRuntimeMultiplayerMapBytes(offer_->request.map, verified, error)) return reject(error);
            bytes_ = std::move(verified); lastChunk_ = chunk; lastAck_ = ack;
        }
    }
    auto action = send(input, chunk.proposalId, RuntimeMultiplayerMessageType::LobbyMapAck, ack);
    action.complete = lastChunk_.has_value(); return action;
}

RuntimeMultiplayerLobbyMapTransfer::Action RuntimeMultiplayerLobbyMapTransfer::pollClient(const Input& input, TimePoint now) {
    if (requested_ && !matches(input.currentMap, requested_->map)) {
        requested_.reset(); offer_.reset(); bytes_.clear(); assembler_.reset(); lastChunk_.reset();
    }
    if (input.lobby && requested_ && matches(input.currentMap, requested_->map)
        && bytes_.empty() && now - requestedAt_ >= std::chrono::seconds(180))
        return failed(input, requested_->requestId, Failure::Protocol, "requested map transfer timed out");
    return {};
}
const RuntimeMultiplayerMapOffer* RuntimeMultiplayerLobbyMapTransfer::offer() const { return offer_ ? &*offer_ : nullptr; }
const std::vector<std::uint8_t>& RuntimeMultiplayerLobbyMapTransfer::bytes() const { return bytes_; }
