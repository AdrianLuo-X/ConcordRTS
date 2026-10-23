#pragma once

#include "network/protocol/RuntimeMultiplayerProtocol.h"

#include <chrono>
#include <optional>
#include <span>

// Lobby-only opaque map transport. It owns no session, socket, filesystem,
// clock or callbacks. Input messages use the existing bounded protocol DTOs;
// the caller performs wire decoding and executes returned actions in order.
class RuntimeMultiplayerLobbyMapTransfer {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    class HostPeer {
        friend class RuntimeMultiplayerLobbyMapTransfer;
        std::optional<RuntimeMultiplayerMapRequest> request_;
        RuntimeMultiplayerSnapshotChunkProducer producer_;
        bool offerSent_ = false;
        bool chunkSent_ = false;
        TimePoint startedAt_{};
        TimePoint chunkSentAt_{};
    };

    struct Input {
        std::uint64_t peerId = 0; // Transport identity; zero denotes the host on a client.
        bool lobby = false;
        bool admitted = false;
        bool ready = false;
        const RuntimeMultiplayerMatchMap* currentMap = nullptr; // Borrowed for this call only.
    };

    enum class Failure { None, Protocol, Transport };
    struct Action {
        bool handled = false;
        std::uint64_t peerId = 0;
        std::uint64_t transferId = 0;
        std::optional<RuntimeMultiplayerMessage> send;
        Failure failure = Failure::None;
        std::string reason;
        bool complete = false;
    };

    void reset();
    bool publish(RuntimeMultiplayerMatchMap map, std::vector<std::uint8_t> bytes, std::string& error);
    bool hostComplete(const HostPeer&, const RuntimeMultiplayerMatchMap* currentMap) const;
    Action receiveHost(HostPeer&, const Input&, const RuntimeMultiplayerMessage&, TimePoint now);
    // At most offer then one chunk per poll. Confirm each successful enqueue
    // before asking for the next action; a failed send stops that peer's poll.
    Action pollHost(HostPeer&, const Input&, TimePoint now);
    void sentHost(HostPeer&, const Action&, TimePoint now);

    bool alreadyRequested(const RuntimeMultiplayerMatchMap&) const;
    bool clientIncomplete(const RuntimeMultiplayerMatchMap* currentMap) const;
    bool prepareRequest(const RuntimeMultiplayerMatchMap&, RuntimeMultiplayerMapRequest&, std::string& error) const;
    // Commit only after Ready=false and the request were successfully queued.
    void requested(RuntimeMultiplayerMapRequest request, TimePoint now);
    Action receiveClient(const Input&, const RuntimeMultiplayerMessage&);
    Action pollClient(const Input&, TimePoint now);
    const RuntimeMultiplayerMapOffer* offer() const;
    const std::vector<std::uint8_t>& bytes() const;

private:
    std::optional<RuntimeMultiplayerMatchMap> hostedMap_;
    std::vector<std::uint8_t> hostedBytes_;
    std::optional<RuntimeMultiplayerMapRequest> requested_;
    std::optional<RuntimeMultiplayerMapOffer> offer_;
    std::vector<std::uint8_t> bytes_;
    RuntimeMultiplayerSnapshotChunkAssembler assembler_;
    std::optional<RuntimeMultiplayerSnapshotChunk> lastChunk_;
    RuntimeMultiplayerSnapshotChunkAck lastAck_;
    std::uint64_t nextRequestId_ = 1;
    TimePoint requestedAt_{};
};
