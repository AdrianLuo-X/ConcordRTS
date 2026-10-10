#include "network/lockstep/RuntimeMultiplayerLockstep.h"

#include <algorithm>
#include <limits>
#include <tuple>
#include <utility>

namespace {

class ByteWriter {
public:
    void writeU16(std::uint16_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value >> 8U));
        bytes_.push_back(static_cast<std::uint8_t>(value));
    }

    void writeU32(std::uint32_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value >> 24U));
        bytes_.push_back(static_cast<std::uint8_t>(value >> 16U));
        bytes_.push_back(static_cast<std::uint8_t>(value >> 8U));
        bytes_.push_back(static_cast<std::uint8_t>(value));
    }

    void writeBytes(std::span<const std::uint8_t> bytes) {
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }

    const std::vector<std::uint8_t>& bytes() const {
        return bytes_;
    }

private:
    std::vector<std::uint8_t> bytes_;
};

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> bytes)
        : bytes_(bytes) {}

    bool readU16(std::uint16_t& value) {
        if (!require(2)) return false;
        value = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes_[offset_]) << 8U)
            | static_cast<std::uint16_t>(bytes_[offset_ + 1])
        );
        offset_ += 2;
        return true;
    }

    bool readU32(std::uint32_t& value) {
        if (!require(4)) return false;
        value =
            (static_cast<std::uint32_t>(bytes_[offset_]) << 24U)
            | (static_cast<std::uint32_t>(bytes_[offset_ + 1]) << 16U)
            | (static_cast<std::uint32_t>(bytes_[offset_ + 2]) << 8U)
            | static_cast<std::uint32_t>(bytes_[offset_ + 3]);
        offset_ += 4;
        return true;
    }

    bool readBytes(
        std::size_t count,
        std::span<const std::uint8_t>& value
    ) {
        if (!require(count)) return false;
        value = bytes_.subspan(offset_, count);
        offset_ += count;
        return true;
    }

    bool finished() const {
        return offset_ == bytes_.size();
    }

    const std::string& error() const {
        return error_;
    }

private:
    bool require(std::size_t count) {
        if (count <= bytes_.size() - offset_) return true;
        if (error_.empty()) {
            error_ = "gameplay frame payload is truncated";
        }
        return false;
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
    std::string error_;
};

bool fail(std::string& errorMessage, std::string message) {
    errorMessage = std::move(message);
    return false;
}

RuntimeMultiplayerOperationResult acceptedOperation(
    bool changed,
    bool duplicate = false
) {
    RuntimeMultiplayerOperationResult result;
    result.accepted = true;
    result.changed = changed;
    result.duplicate = duplicate;
    return result;
}

RuntimeMultiplayerOperationResult rejectedOperation(std::string reason) {
    RuntimeMultiplayerOperationResult result;
    result.reason = std::move(reason);
    return result;
}

bool validCodecLimits(
    std::size_t maximumCommands,
    std::size_t maximumBytes,
    std::string& errorMessage
) {
    if (maximumCommands
        > runtimeMultiplayerLockstepMaximumCommandsPerFrame) {
        return fail(
            errorMessage,
            "gameplay frame command limit exceeds the protocol maximum"
        );
    }
    if (maximumBytes < runtimeMultiplayerGameplayFrameHeaderBytes
        || maximumBytes > runtimeMultiplayerLockstepMaximumFrameBytes) {
        return fail(
            errorMessage,
            "gameplay frame byte limit is invalid"
        );
    }
    return true;
}

class StableHash {
public:
    void addByte(std::uint8_t value) {
        value_ ^= static_cast<std::uint64_t>(value);
        value_ *= 1099511628211ULL;
    }

    void addU64(std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            addByte(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void addInt(int value) {
        addU64(static_cast<std::uint64_t>(
            static_cast<std::int64_t>(value)
        ));
    }

    void addSize(std::size_t value) {
        addU64(static_cast<std::uint64_t>(value));
    }

    void addString(const std::string& value) {
        addSize(value.size());
        for (unsigned char byte : value) {
            addByte(static_cast<std::uint8_t>(byte));
        }
    }

    void addBytes(const std::vector<std::uint8_t>& value) {
        addSize(value.size());
        for (std::uint8_t byte : value) addByte(byte);
    }

    std::uint64_t value() const {
        return value_;
    }

private:
    std::uint64_t value_ = 1469598103934665603ULL;
};

} // namespace

bool canonicalizeRuntimeMultiplayerGameplayFrame(
    RuntimeMultiplayerGameplayFrame& frame,
    std::string& errorMessage,
    std::size_t maximumCommands
) {
    if (maximumCommands
            > runtimeMultiplayerLockstepMaximumCommandsPerFrame
        || frame.commands.size() > maximumCommands) {
        return fail(
            errorMessage,
            "gameplay frame command count exceeds the size limit"
        );
    }

    RuntimeMultiplayerGameplayFrame canonical = frame;
    for (RuntimeMultiplayerGameplayCommand& command : canonical.commands) {
        std::string commandError;
        if (!canonicalizeRuntimeMultiplayerGameplayCommand(
                command,
                commandError
            )) {
            return fail(
                errorMessage,
                "gameplay frame contains an invalid command: " + commandError
            );
        }
    }
    frame = std::move(canonical);
    errorMessage.clear();
    return true;
}

bool encodeRuntimeMultiplayerGameplayFrame(
    const RuntimeMultiplayerGameplayFrame& frame,
    std::vector<std::uint8_t>& payload,
    std::string& errorMessage,
    std::size_t maximumCommands,
    std::size_t maximumBytes
) {
    std::string encodeError;
    if (!validCodecLimits(maximumCommands, maximumBytes, encodeError)) {
        errorMessage = std::move(encodeError);
        return false;
    }

    RuntimeMultiplayerGameplayFrame canonical = frame;
    if (!canonicalizeRuntimeMultiplayerGameplayFrame(
            canonical,
            encodeError,
            maximumCommands
        )) {
        errorMessage = std::move(encodeError);
        return false;
    }

    ByteWriter writer;
    writer.writeU32(runtimeMultiplayerGameplayFrameMagic);
    writer.writeU16(runtimeMultiplayerGameplayFrameVersion);
    writer.writeU16(static_cast<std::uint16_t>(canonical.commands.size()));
    for (const RuntimeMultiplayerGameplayCommand& command :
            canonical.commands) {
        std::vector<std::uint8_t> commandPayload;
        std::string commandError;
        if (!encodeRuntimeMultiplayerGameplayCommand(
                command,
                commandPayload,
                commandError
            )) {
            errorMessage =
                "gameplay frame command encoding failed: " + commandError;
            return false;
        }
        writer.writeU32(static_cast<std::uint32_t>(commandPayload.size()));
        writer.writeBytes(commandPayload);
        if (writer.bytes().size() > maximumBytes) {
            errorMessage = "gameplay frame payload exceeds the size limit";
            return false;
        }
    }

    payload = writer.bytes();
    errorMessage.clear();
    return true;
}

bool decodeRuntimeMultiplayerGameplayFrame(
    std::span<const std::uint8_t> payload,
    RuntimeMultiplayerGameplayFrame& frame,
    std::string& errorMessage,
    std::size_t maximumCommands,
    std::size_t maximumBytes
) {
    std::string decodeError;
    if (!validCodecLimits(maximumCommands, maximumBytes, decodeError)) {
        errorMessage = std::move(decodeError);
        return false;
    }
    if (payload.size() > maximumBytes) {
        return fail(
            errorMessage,
            "gameplay frame payload exceeds the size limit"
        );
    }

    ByteReader reader(payload);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t count = 0;
    if (!reader.readU32(magic)
        || !reader.readU16(version)
        || !reader.readU16(count)) {
        errorMessage = reader.error();
        return false;
    }
    if (magic != runtimeMultiplayerGameplayFrameMagic) {
        return fail(errorMessage, "gameplay frame magic is invalid");
    }
    if (version != runtimeMultiplayerGameplayFrameVersion) {
        return fail(errorMessage, "gameplay frame version is unsupported");
    }
    if (static_cast<std::size_t>(count) > maximumCommands) {
        return fail(
            errorMessage,
            "gameplay frame command count exceeds the size limit"
        );
    }

    RuntimeMultiplayerGameplayFrame parsed;
    parsed.commands.reserve(static_cast<std::size_t>(count));
    for (std::uint16_t index = 0; index < count; ++index) {
        std::uint32_t encodedLength = 0;
        if (!reader.readU32(encodedLength)) {
            errorMessage = reader.error();
            return false;
        }
        if (encodedLength == 0
            || encodedLength
                > static_cast<std::uint32_t>(
                    runtimeMultiplayerGameplayMaximumPayloadBytes
                )) {
            return fail(
                errorMessage,
                "gameplay frame command length is invalid"
            );
        }
        std::span<const std::uint8_t> commandPayload;
        if (!reader.readBytes(
                static_cast<std::size_t>(encodedLength),
                commandPayload
            )) {
            errorMessage = reader.error();
            return false;
        }
        RuntimeMultiplayerGameplayCommand command;
        std::string commandError;
        if (!decodeRuntimeMultiplayerGameplayCommand(
                commandPayload,
                command,
                commandError
            )) {
            return fail(
                errorMessage,
                "gameplay frame contains an invalid command: " + commandError
            );
        }
        parsed.commands.push_back(std::move(command));
    }
    if (!reader.finished()) {
        return fail(errorMessage, "gameplay frame payload has trailing bytes");
    }

    // Nested command decoders require their own canonical form. Re-encoding
    // also guards future command codecs from accidentally adding an alternate
    // representation which a frame decoder would otherwise accept.
    std::vector<std::uint8_t> canonicalPayload;
    if (!encodeRuntimeMultiplayerGameplayFrame(
            parsed,
            canonicalPayload,
            decodeError,
            maximumCommands,
            maximumBytes
        )) {
        return fail(
            errorMessage,
            "gameplay frame canonical validation failed: " + decodeError
        );
    }
    if (!std::equal(
            payload.begin(),
            payload.end(),
            canonicalPayload.begin(),
            canonicalPayload.end()
        )) {
        return fail(errorMessage, "gameplay frame payload is not canonical");
    }

    frame = std::move(parsed);
    errorMessage.clear();
    return true;
}

void RuntimeMultiplayerLockstepDriver::reset() {
    session_ = nullptr;
    mutableSession_ = nullptr;
    config_ = RuntimeMultiplayerLockstepConfig{};
    participants_.clear();
    frames_.clear();
    boundTimelineEpoch_ = 0;
    bindingStartTick_ = -1;
    firstNetworkInputTick_ = -1;
    currentBarrierTick_ = -1;
    expectedAuthoritativeTick_ = -1;
    currentBarrierApplied_ = false;
}

RuntimeMultiplayerOperationResult RuntimeMultiplayerLockstepDriver::bind(
    RuntimeMultiplayerSessionState& session,
    RuntimeMultiplayerLockstepConfig config
) {
    return bindSession(session, &session, config, false);
}

RuntimeMultiplayerOperationResult RuntimeMultiplayerLockstepDriver::bind(
    const RuntimeMultiplayerSessionState& session,
    RuntimeMultiplayerLockstepConfig config
) {
    return bindSession(session, nullptr, config, false);
}

RuntimeMultiplayerOperationResult
RuntimeMultiplayerLockstepDriver::rebindAfterLoadCommit(
    RuntimeMultiplayerSessionState& session,
    RuntimeMultiplayerLockstepConfig config
) {
    return bindSession(session, &session, config, true);
}

RuntimeMultiplayerOperationResult
RuntimeMultiplayerLockstepDriver::rebindAfterLoadCommit(
    const RuntimeMultiplayerSessionState& session,
    RuntimeMultiplayerLockstepConfig config
) {
    return bindSession(session, nullptr, config, true);
}

RuntimeMultiplayerOperationResult RuntimeMultiplayerLockstepDriver::bindSession(
    const RuntimeMultiplayerSessionState& session,
    RuntimeMultiplayerSessionState* mutableSession,
    RuntimeMultiplayerLockstepConfig config,
    bool afterLoadCommit
) {
    if (session.status() != RuntimeMultiplayerRoomStatus::Running) {
        return rejectedOperation(
            "lockstep driver requires a running multiplayer session"
        );
    }
    if (!session.commands().empty()) {
        return rejectedOperation(
            "lockstep driver must bind before gameplay frames are admitted"
        );
    }
    if (!afterLoadCommit && session.timelineEpoch() != 1) {
        return rejectedOperation(
            "initial lockstep bind requires timeline epoch 1"
        );
    }
    if (!afterLoadCommit
        && session.authoritativeTick() != session.startTick()) {
        return rejectedOperation(
            "lockstep driver must bind at the session start tick"
        );
    }
    if (afterLoadCommit && session.timelineEpoch() <= 1) {
        return rejectedOperation(
            "lockstep load rebind requires an advanced timeline epoch"
        );
    }
    if (config.inputDelayTicks < 0
        || config.inputDelayTicks
            > runtimeMultiplayerLockstepMaximumInputDelayTicks
        || config.inputDelayTicks > session.config().commandLeadWindowTicks) {
        return rejectedOperation("lockstep input delay is invalid");
    }
    std::string limitError;
    if (!validCodecLimits(
            config.maximumCommandsPerFrame,
            config.maximumFrameBytes,
            limitError
        )
        || config.maximumFrameBytes
            > session.config().maximumCommandPayloadBytes) {
        return rejectedOperation(
            limitError.empty()
                ? "lockstep frame byte limit exceeds the session envelope"
                : limitError
        );
    }

    const std::int64_t firstTick =
        static_cast<std::int64_t>(session.authoritativeTick())
        + static_cast<std::int64_t>(config.inputDelayTicks);
    if (firstTick < 0
        || firstTick
            > static_cast<std::int64_t>(runtimeMultiplayerMaximumTick)) {
        return rejectedOperation("lockstep first gameplay tick is invalid");
    }

    std::vector<Participant> participants;
    participants.reserve(session.members().size());
    std::size_t combatants = 0;
    for (const RuntimeMultiplayerRoomMember& member : session.members()) {
        Participant participant;
        participant.peerId = member.peerId;
        participant.participation = member.participation;
        combatants += member.participation
                == RuntimeMultiplayerParticipationRole::Combatant
            ? 1U
            : 0U;
        participants.push_back(std::move(participant));
    }
    if (combatants == 0) {
        return rejectedOperation("lockstep room has no combatants");
    }
    std::sort(
        participants.begin(),
        participants.end(),
        [](const Participant& left, const Participant& right) {
            return left.peerId < right.peerId;
        }
    );
    const auto duplicate = std::adjacent_find(
        participants.begin(),
        participants.end(),
        [](const Participant& left, const Participant& right) {
            return left.peerId == right.peerId;
        }
    );
    if (duplicate != participants.end()) {
        return rejectedOperation("lockstep room member ids are not unique");
    }

    session_ = &session;
    mutableSession_ = mutableSession;
    config_ = config;
    participants_ = std::move(participants);
    frames_.clear();
    boundTimelineEpoch_ = session.timelineEpoch();
    bindingStartTick_ = session.authoritativeTick();
    firstNetworkInputTick_ = static_cast<int>(firstTick);
    currentBarrierTick_ = session.authoritativeTick();
    expectedAuthoritativeTick_ = session.authoritativeTick();
    currentBarrierApplied_ = false;
    return acceptedOperation(true);
}

RuntimeMultiplayerOperationResult
RuntimeMultiplayerLockstepDriver::submitFrame(
    const RuntimeMultiplayerCommandPacket& packet
) {
    return admitFrame(packet, false);
}

RuntimeMultiplayerOperationResult
RuntimeMultiplayerLockstepDriver::observeAdmittedFrame(
    const RuntimeMultiplayerCommandPacket& packet
) {
    return admitFrame(packet, true);
}

RuntimeMultiplayerOperationResult
RuntimeMultiplayerLockstepDriver::admitFrame(
    const RuntimeMultiplayerCommandPacket& packet,
    bool observeAlreadyAdmitted
) {
    if (!bound() || !bindingStillMatches()) {
        return rejectedOperation("lockstep driver/session binding is invalid");
    }
    if (packet.timelineEpoch == 0
        || packet.timelineEpoch != boundTimelineEpoch_) {
        return rejectedOperation("lockstep frame timeline epoch is stale");
    }
    if (!observeAlreadyAdmitted && mutableSession_ == nullptr) {
        return rejectedOperation(
            "direct lockstep submission requires a mutable session binding"
        );
    }
    Participant* participant = findParticipant(packet.peerId);
    if (participant == nullptr) {
        return rejectedOperation("lockstep frame peer is not a room member");
    }
    if (participant->participation
        != RuntimeMultiplayerParticipationRole::Combatant) {
        return rejectedOperation("spectators may not submit lockstep frames");
    }
    if (packet.tick < currentBarrierTick_
        || (packet.tick == currentBarrierTick_
            && currentBarrierApplied_)) {
        return rejectedOperation(
            "completed lockstep ticks may not be replayed"
        );
    }

    if (observeAlreadyAdmitted
        && std::find(
            session_->commands().begin(),
            session_->commands().end(),
            packet
        ) == session_->commands().end()) {
        return rejectedOperation(
            "lockstep frame envelope is not admitted by the bound session"
        );
    }

    if (const StoredFrame* existing = findFrame(packet.tick, packet.peerId)) {
        if (existing->packet == packet) {
            return acceptedOperation(false, true);
        }
        return rejectedOperation(
            "combatant already submitted a different frame for this tick"
        );
    }

    const std::int64_t minimumAdmissionTick =
        static_cast<std::int64_t>(session_->authoritativeTick())
        + static_cast<std::int64_t>(config_.inputDelayTicks);
    if (packet.tick < firstNetworkInputTick_
        || static_cast<std::int64_t>(packet.tick) < minimumAdmissionTick) {
        return rejectedOperation(
            "lockstep frame missed the fixed input-delay window"
        );
    }
    if (packet.payload.size() > config_.maximumFrameBytes) {
        return rejectedOperation(
            "lockstep frame payload exceeds the configured byte limit"
        );
    }

    RuntimeMultiplayerGameplayFrame frame;
    std::string decodeError;
    if (!decodeRuntimeMultiplayerGameplayFrame(
            packet.payload,
            frame,
            decodeError,
            config_.maximumCommandsPerFrame,
            config_.maximumFrameBytes
        )) {
        return rejectedOperation(
            "lockstep frame payload is invalid: " + decodeError
        );
    }
    if (packet.sequence <= participant->lastSequence) {
        return rejectedOperation(
            "lockstep frame sequence is not strictly increasing"
        );
    }

    if (!observeAlreadyAdmitted) {
        const RuntimeMultiplayerOperationResult sessionResult =
            mutableSession_->submitCommand(packet);
        if (!sessionResult.accepted) return sessionResult;
        if (sessionResult.duplicate) {
            return rejectedOperation(
                "lockstep frame envelope was admitted outside this driver"
            );
        }
    }

    StoredFrame stored;
    stored.packet = packet;
    stored.frame = std::move(frame);
    frames_.push_back(std::move(stored));
    std::sort(
        frames_.begin(),
        frames_.end(),
        [](const StoredFrame& left, const StoredFrame& right) {
            return std::tie(left.packet.tick, left.packet.peerId)
                < std::tie(right.packet.tick, right.packet.peerId);
        }
    );
    participant->lastSequence = packet.sequence;
    return acceptedOperation(true);
}

bool RuntimeMultiplayerLockstepDriver::frameBarrierReady(int tick) const {
    if (!bound() || !bindingStillMatches()
        || session_->hasActiveSaveLoadHandshake()
        || tick != currentBarrierTick_ || currentBarrierApplied_) {
        return false;
    }
    if (tick < firstNetworkInputTick_) return true;
    return std::all_of(
        participants_.begin(),
        participants_.end(),
        [this, tick](const Participant& participant) {
            return participant.participation
                    != RuntimeMultiplayerParticipationRole::Combatant
                || findFrame(tick, participant.peerId) != nullptr;
        }
    );
}

bool RuntimeMultiplayerLockstepDriver::inputHorizonReady() const {
    return bound() && bindingStillMatches()
        && !session_->hasActiveSaveLoadHandshake()
        && hasCompleteInputHorizon();
}

bool RuntimeMultiplayerLockstepDriver::canonicalApplyList(
    int tick,
    std::vector<RuntimeMultiplayerLockstepApplyCommand>& applyCommands,
    std::string& errorMessage
) const {
    if (!bound() || !bindingStillMatches()) {
        return fail(errorMessage, "lockstep driver/session binding is invalid");
    }
    if (tick != currentBarrierTick_) {
        return fail(errorMessage, "lockstep tick is not the current barrier");
    }
    if (currentBarrierApplied_) {
        return fail(errorMessage, "lockstep tick has already been applied");
    }
    if (!frameBarrierReady(tick)) {
        return fail(errorMessage, "lockstep frame barrier is incomplete");
    }

    std::vector<RuntimeMultiplayerLockstepApplyCommand> canonical;
    if (tick < firstNetworkInputTick_) {
        applyCommands = std::move(canonical);
        errorMessage.clear();
        return true;
    }
    for (const Participant& participant : participants_) {
        if (participant.participation
            != RuntimeMultiplayerParticipationRole::Combatant) {
            continue;
        }
        const StoredFrame* stored = findFrame(tick, participant.peerId);
        if (stored == nullptr) {
            return fail(errorMessage, "lockstep frame barrier is incomplete");
        }
        canonical.reserve(canonical.size() + stored->frame.commands.size());
        for (std::size_t index = 0;
                index < stored->frame.commands.size();
                ++index) {
            RuntimeMultiplayerLockstepApplyCommand item;
            item.tick = tick;
            item.peerId = participant.peerId;
            item.frameSequence = stored->packet.sequence;
            item.commandIndex = index;
            item.command = stored->frame.commands[index];
            canonical.push_back(std::move(item));
        }
    }
    applyCommands = std::move(canonical);
    errorMessage.clear();
    return true;
}

RuntimeMultiplayerOperationResult
RuntimeMultiplayerLockstepDriver::applyReadyTick(
    int tick,
    const RuntimeMultiplayerLockstepBatchCallback& prevalidate,
    const RuntimeMultiplayerLockstepBatchCallback& apply
) {
    if (!apply) {
        return rejectedOperation("lockstep apply callback is required");
    }
    std::vector<RuntimeMultiplayerLockstepApplyCommand> commands;
    std::string error;
    if (!canonicalApplyList(tick, commands, error)) {
        return rejectedOperation(std::move(error));
    }

    const std::span<const RuntimeMultiplayerLockstepApplyCommand> batch(
        commands
    );
    if (prevalidate && !prevalidate(batch, error)) {
        return rejectedOperation(
            error.empty()
                ? "lockstep batch prevalidation failed"
                : std::move(error)
        );
    }
    error.clear();
    if (!apply(batch, error)) {
        return rejectedOperation(
            error.empty()
                ? "lockstep batch application failed"
                : std::move(error)
        );
    }

    currentBarrierApplied_ = true;
    return acceptedOperation(true);
}

RuntimeMultiplayerOperationResult
RuntimeMultiplayerLockstepDriver::advanceHost(
    const std::string& requestingPeerId,
    int completedTick
) {
    if (!bound() || !bindingStillMatches()) {
        return rejectedOperation("lockstep driver/session binding is invalid");
    }
    if (mutableSession_ == nullptr) {
        return rejectedOperation(
            "direct host advance requires a mutable session binding"
        );
    }
    if (requestingPeerId != session_->config().hostPeerId) {
        return rejectedOperation("only the host may advance lockstep ticks");
    }
    if (completedTick == currentBarrierTick_ - 1
        && !currentBarrierApplied_
        && session_->authoritativeTick() == currentBarrierTick_) {
        return acceptedOperation(false, true);
    }
    if (completedTick != currentBarrierTick_ || !currentBarrierApplied_) {
        return rejectedOperation(
            "lockstep host advance requires the applied current barrier"
        );
    }
    if (!hasCompleteInputHorizon()) {
        return rejectedOperation(
            "lockstep input horizon is incomplete"
        );
    }
    if (completedTick >= runtimeMultiplayerMaximumTick) {
        return rejectedOperation("lockstep host advance tick would overflow");
    }

    const RuntimeMultiplayerOperationResult advanced =
        mutableSession_->advanceAuthoritativeTick(
            requestingPeerId,
            completedTick + 1,
            boundTimelineEpoch_
        );
    if (!advanced.accepted) return advanced;
    commitAuthoritativeAdvance(completedTick);
    return advanced;
}

RuntimeMultiplayerOperationResult
RuntimeMultiplayerLockstepDriver::acknowledgeAuthoritativeAdvance(
    int completedTick
) {
    if (!bound() || !membershipBindingStillMatches()) {
        return rejectedOperation("lockstep driver/session binding is invalid");
    }
    if (completedTick == currentBarrierTick_ - 1
        && !currentBarrierApplied_
        && session_->authoritativeTick() == currentBarrierTick_
        && expectedAuthoritativeTick_ == currentBarrierTick_) {
        return acceptedOperation(false, true);
    }
    if (completedTick != currentBarrierTick_ || !currentBarrierApplied_) {
        return rejectedOperation(
            "replicated advance requires the applied current barrier"
        );
    }
    if (completedTick >= runtimeMultiplayerMaximumTick
        || session_->authoritativeTick() != completedTick + 1) {
        return rejectedOperation(
            "replicated authoritative tick does not follow the barrier"
        );
    }
    if (!hasCompleteInputHorizon()) {
        return rejectedOperation(
            "replicated advance has an incomplete input horizon"
        );
    }
    commitAuthoritativeAdvance(completedTick);
    return acceptedOperation(true);
}

bool RuntimeMultiplayerLockstepDriver::bound() const {
    return session_ != nullptr && boundTimelineEpoch_ != 0
        && currentBarrierTick_ >= 0;
}

std::uint64_t RuntimeMultiplayerLockstepDriver::timelineEpoch() const {
    return boundTimelineEpoch_;
}

int RuntimeMultiplayerLockstepDriver::currentBarrierTick() const {
    return currentBarrierTick_;
}

int RuntimeMultiplayerLockstepDriver::firstNetworkInputTick() const {
    return firstNetworkInputTick_;
}

bool RuntimeMultiplayerLockstepDriver::tickCompleted(int tick) const {
    if (!bound() || tick < bindingStartTick_) return false;
    return tick < currentBarrierTick_
        || (tick == currentBarrierTick_ && currentBarrierApplied_);
}

const RuntimeMultiplayerLockstepConfig&
RuntimeMultiplayerLockstepDriver::config() const {
    return config_;
}

std::uint64_t RuntimeMultiplayerLockstepDriver::stateHash() const {
    StableHash hash;
    hash.addU64(runtimeMultiplayerGameplayFrameMagic);
    hash.addU64(runtimeMultiplayerGameplayFrameVersion);
    hash.addInt(config_.inputDelayTicks);
    hash.addSize(config_.maximumCommandsPerFrame);
    hash.addSize(config_.maximumFrameBytes);
    hash.addU64(boundTimelineEpoch_);
    hash.addInt(bindingStartTick_);
    hash.addInt(firstNetworkInputTick_);
    hash.addInt(currentBarrierTick_);
    hash.addInt(expectedAuthoritativeTick_);
    hash.addByte(currentBarrierApplied_ ? 1U : 0U);
    hash.addSize(participants_.size());
    for (const Participant& participant : participants_) {
        hash.addString(participant.peerId);
        hash.addByte(static_cast<std::uint8_t>(participant.participation));
        hash.addU64(participant.lastSequence);
    }
    hash.addSize(frames_.size());
    for (const StoredFrame& stored : frames_) {
        hash.addInt(stored.packet.tick);
        hash.addString(stored.packet.peerId);
        hash.addU64(stored.packet.sequence);
        hash.addU64(stored.packet.timelineEpoch);
        hash.addBytes(stored.packet.payload);
    }
    if (session_ != nullptr) hash.addU64(session_->stateHash());
    return hash.value();
}

RuntimeMultiplayerLockstepDriver::Participant*
RuntimeMultiplayerLockstepDriver::findParticipant(
    const std::string& peerId
) {
    const auto found = std::lower_bound(
        participants_.begin(),
        participants_.end(),
        peerId,
        [](const Participant& participant, const std::string& value) {
            return participant.peerId < value;
        }
    );
    return found != participants_.end() && found->peerId == peerId
        ? &*found
        : nullptr;
}

const RuntimeMultiplayerLockstepDriver::Participant*
RuntimeMultiplayerLockstepDriver::findParticipant(
    const std::string& peerId
) const {
    const auto found = std::lower_bound(
        participants_.begin(),
        participants_.end(),
        peerId,
        [](const Participant& participant, const std::string& value) {
            return participant.peerId < value;
        }
    );
    return found != participants_.end() && found->peerId == peerId
        ? &*found
        : nullptr;
}

RuntimeMultiplayerLockstepDriver::StoredFrame*
RuntimeMultiplayerLockstepDriver::findFrame(
    int tick,
    const std::string& peerId
) {
    const auto found = std::lower_bound(
        frames_.begin(),
        frames_.end(),
        std::tie(tick, peerId),
        [](const StoredFrame& frame, const auto& key) {
            return std::tie(frame.packet.tick, frame.packet.peerId) < key;
        }
    );
    return found != frames_.end()
            && found->packet.tick == tick
            && found->packet.peerId == peerId
        ? &*found
        : nullptr;
}

const RuntimeMultiplayerLockstepDriver::StoredFrame*
RuntimeMultiplayerLockstepDriver::findFrame(
    int tick,
    const std::string& peerId
) const {
    const auto found = std::lower_bound(
        frames_.begin(),
        frames_.end(),
        std::tie(tick, peerId),
        [](const StoredFrame& frame, const auto& key) {
            return std::tie(frame.packet.tick, frame.packet.peerId) < key;
        }
    );
    return found != frames_.end()
            && found->packet.tick == tick
            && found->packet.peerId == peerId
        ? &*found
        : nullptr;
}

void RuntimeMultiplayerLockstepDriver::commitAuthoritativeAdvance(
    int completedTick
) {
    const auto firstRetainedFrame = std::upper_bound(
        frames_.begin(),
        frames_.end(),
        completedTick,
        [](int tick, const StoredFrame& frame) {
            return tick < frame.packet.tick;
        }
    );
    frames_.erase(frames_.begin(), firstRetainedFrame);
    currentBarrierTick_ = completedTick + 1;
    expectedAuthoritativeTick_ = completedTick + 1;
    currentBarrierApplied_ = false;
}

bool RuntimeMultiplayerLockstepDriver::hasCompleteInputHorizon() const {
    const std::int64_t horizonTick =
        static_cast<std::int64_t>(currentBarrierTick_)
        + static_cast<std::int64_t>(config_.inputDelayTicks);
    if (horizonTick < 0
        || horizonTick
            > static_cast<std::int64_t>(runtimeMultiplayerMaximumTick)) {
        return false;
    }
    return std::all_of(
        participants_.begin(),
        participants_.end(),
        [this, horizonTick](const Participant& participant) {
            return participant.participation
                    != RuntimeMultiplayerParticipationRole::Combatant
                || findFrame(
                    static_cast<int>(horizonTick),
                    participant.peerId
                ) != nullptr;
        }
    );
}

bool RuntimeMultiplayerLockstepDriver::membershipBindingStillMatches() const {
    if (session_ == nullptr
        || session_->status() != RuntimeMultiplayerRoomStatus::Running
        || session_->timelineEpoch() != boundTimelineEpoch_
        || session_->members().size() > participants_.size()) {
        return false;
    }
    for (const RuntimeMultiplayerRoomMember& member : session_->members()) {
        const Participant* participant = findParticipant(member.peerId);
        if (participant == nullptr
            || participant->participation != member.participation) {
            return false;
        }
    }
    // Running membership is immutable for combatants. Session may, however,
    // canonically remove an observer and record a sorted anti-replay tombstone.
    // Retain that observer in the original driver binding (observers never
    // contribute input barriers), but accept only this explicit, monotonic
    // spectator-only contraction. New peers, role changes, or missing
    // combatants still invalidate the binding.
    for (const Participant& participant : participants_) {
        const auto member = std::find_if(
            session_->members().begin(),
            session_->members().end(),
            [&participant](const RuntimeMultiplayerRoomMember& value) {
                return value.peerId == participant.peerId;
            }
        );
        if (member != session_->members().end()) continue;
        if (participant.participation
                != RuntimeMultiplayerParticipationRole::Spectator
            || !std::binary_search(
                session_->departedSpectatorPeerIds().begin(),
                session_->departedSpectatorPeerIds().end(),
                participant.peerId
            )) {
            return false;
        }
    }
    return true;
}

bool RuntimeMultiplayerLockstepDriver::bindingStillMatches() const {
    return membershipBindingStillMatches()
        && session_->authoritativeTick() == expectedAuthoritativeTick_;
}
