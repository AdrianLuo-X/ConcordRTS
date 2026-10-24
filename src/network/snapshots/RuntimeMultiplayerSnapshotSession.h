#pragma once
#include "network/session/RuntimeMultiplayerLanSession.h"

// Only snapshot transport side effects. Borrowed synchronously for one call;
// no socket/native handle, WindowState, scheduler or filesystem is exposed.
struct RuntimeMultiplayerSnapshotSessionPort
{
    virtual ~RuntimeMultiplayerSnapshotSessionPort() = default;
    virtual bool sendToHost(const RuntimeMultiplayerMessage &, std::string &) = 0;
    virtual bool sendPacketToHost(const RuntimeNetworkPacket &, std::string &) = 0;
    virtual bool broadcastPacket(const RuntimeNetworkPacket &, std::string &) = 0;
    virtual void protocolFailure(std::string, std::vector<RuntimeMultiplayerLanEvent> &) = 0;
    virtual void appendAppliedEvent(const RuntimeMultiplayerMessage &,
                                    const RuntimeMultiplayerMessageApplyResult &,
                                    std::vector<RuntimeMultiplayerLanEvent> &) = 0;
    virtual void clearSnapshotTransfer(bool) noexcept = 0;
    virtual void abortHost(RuntimeMultiplayerSessionAbortReason) = 0;
    virtual RuntimeMultiplayerLanOperationResult applyHostMessage(RuntimeMultiplayerMessage) = 0;
};

// Owns transfer/verified-candidate/commit state, not canonical SessionState or
// native transport. The four explicit domain borrows outlive this component;
// Impl stays at a stable unique_ptr address. State views never cross reset.
class RuntimeMultiplayerSnapshotSession
{
  public:
    struct State
    {
        bool snapshotTransferActive = false;
        bool snapshotStaged = false;
        RuntimeMultiplayerSaveLoadProposal snapshotProposal;
        RuntimeMultiplayerSnapshotChunkAssembler snapshotAssembler;
        bool haveCompletedSnapshotChunk = false;
        RuntimeMultiplayerSnapshotChunk completedSnapshotChunk;
        RuntimeMultiplayerSnapshotChunkAck completedSnapshotAck;
        std::uint64_t stagedAuthoritativeStateHash = 0;
        bool snapshotCommitCompleted = false;
        std::uint64_t committedAuthoritativeStateHash = 0;
        std::uint64_t snapshotSendPollCounter = 0;
    };
    RuntimeMultiplayerSnapshotSession(RuntimeMultiplayerSessionState &state,
                                      const RuntimeMultiplayerLanSnapshotHooks &hooks,
                                      const std::string &peer,
                                      RuntimeMultiplayerLanPhase &currentPhase)
        : session(state), snapshotHooks(hooks), localPeerId(peer), phase(currentPhase)
    {
    }
    RuntimeMultiplayerSnapshotSession(const RuntimeMultiplayerSnapshotSession &) = delete;
    RuntimeMultiplayerSnapshotSession &operator=(const RuntimeMultiplayerSnapshotSession &) =
        delete;
    State &state() noexcept
    {
        return data_;
    }
    const State &state() const noexcept
    {
        return data_;
    }
    const RuntimeMultiplayerSaveLoadHandshake *activeSaveLoad() const;
    void discardStaged(bool) const noexcept;
    void resetState() noexcept;
    bool stageSnapshotBytes(const RuntimeMultiplayerSaveLoadProposal &,
                            std::span<const std::uint8_t>, std::uint64_t &, std::string &) const;
    bool commitSnapshot(std::uint64_t &, std::string &) const;
    bool postSnapshotSessionCommit(std::string &) const;
    void handleClientSnapshotChunk(const RuntimeMultiplayerSnapshotChunk &,
                                   RuntimeMultiplayerSnapshotSessionPort &,
                                   std::vector<RuntimeMultiplayerLanEvent> &);
    void handleClientSnapshotCommit(const RuntimeNetworkPacket &, const RuntimeMultiplayerMessage &,
                                    RuntimeMultiplayerSnapshotSessionPort &,
                                    std::vector<RuntimeMultiplayerLanEvent> &);
    void finishSnapshotTerminalState(RuntimeMultiplayerSnapshotSessionPort &, bool &,
                                     std::uint64_t &);
    RuntimeMultiplayerLanOperationResult issueHostSnapshotCommit(
        RuntimeMultiplayerSnapshotSessionPort &, std::vector<RuntimeMultiplayerLanEvent> &);
    enum class HostChunk
    {
        Skip,
        Ready,
        Error
    };
    bool beginHostPoll();
    HostChunk nextHostChunk(bool, const RuntimeMultiplayerSnapshotChunkProducer &, bool,
                            std::uint64_t, RuntimeMultiplayerSnapshotChunk &, std::string &) const;
    void sentHostChunk(bool &, std::uint64_t &) const;

  private:
    State data_;
    RuntimeMultiplayerSessionState &session;
    const RuntimeMultiplayerLanSnapshotHooks &snapshotHooks;
    const std::string &localPeerId;
    RuntimeMultiplayerLanPhase &phase;
};
