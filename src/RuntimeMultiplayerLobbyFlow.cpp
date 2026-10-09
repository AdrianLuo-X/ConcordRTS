#include "RuntimeMultiplayerLobbyFlow.h"
#include "RuntimeMultiplayerMapCache.h"

#include "RuntimeMultiplayerCompatibilityFingerprint.h"

#include <algorithm>
#include <utility>

namespace {

bool fail(std::string& errorMessage, std::string reason) {
    errorMessage = std::move(reason);
    return false;
}

RuntimeMultiplayerLanOperationResult failedOperation(std::string reason) {
    RuntimeMultiplayerLanOperationResult result;
    result.reason = std::move(reason);
    return result;
}

bool expectedIncompleteDraftError(const std::string& errorMessage) {
    return errorMessage.find("at least two combatants") != std::string::npos;
}

} // namespace

RuntimeMultiplayerLobbyFlow::RuntimeMultiplayerLobbyFlow(
    RuntimeMultiplayerLockstepConfig lockstepConfig
) : lockstepConfig_(std::move(lockstepConfig)) {}

bool RuntimeMultiplayerLobbyFlow::startHost(
    RuntimeMultiplayerLanHostOptions networkOptions,
    const RuntimeMultiplayerLobbyHostCreateOptions& lobbyOptions,
    std::string& errorMessage,
    const RuntimeMultiplayerResumeSave* resumeSave
) {
    close();
    if (networkOptions.session.hostPeerId
            != lobbyOptions.hostMember.peerId
        || networkOptions.session.hostDisplayName
            != lobbyOptions.hostMember.displayName
        || networkOptions.session.hostParticipation
            != lobbyOptions.hostMember.participation) {
        return fail(
            errorMessage,
            "network host identity does not match the lobby host member"
        );
    }

    RuntimeMultiplayerLobbyModel lobbyCandidate;
    if (resumeSave) {
        RuntimeMultiplayerResumeSave verified;
        if (!readRuntimeMultiplayerResumeSave(resumeSave->bytes, verified, errorMessage)) return false;
        if (verified.setup != resumeSave->setup || verified.descriptor != resumeSave->descriptor
            || verified.compatibility != networkOptions.session.compatibility)
            return fail(errorMessage, "resume save differs from the local version, content, or room recipe");
        const auto oldHost = std::find_if(verified.setup.members.begin(), verified.setup.members.end(),
            [&](const auto& member) { return member.peerId == verified.setup.hostPeerId; });
        const bool oldSpectator = oldHost->identity == RuntimeMultiplayerMatchMemberIdentity::Spectator;
        if (oldSpectator != (lobbyOptions.hostMember.participation == RuntimeMultiplayerParticipationRole::Spectator))
            return fail(errorMessage, "resume host participation differs from the saved host");
        if (!RuntimeMultiplayerLobbyModel::createResumeHost(verified.setup, lobbyOptions.hostMember, lobbyCandidate, errorMessage)) return false;
        resumeSetup_ = std::move(verified.setup);
        resumeManifestVerified_ = true;
    } else if (!RuntimeMultiplayerLobbyModel::createHost(
            lobbyOptions,
            lobbyCandidate,
            errorMessage
        )) {
        return false;
    }
    const std::size_t slotCount = lobbyCandidate.setup().slots.size();
    const std::size_t computerCount = static_cast<std::size_t>(
        std::count_if(
            lobbyCandidate.setup().slots.begin(),
            lobbyCandidate.setup().slots.end(),
            [](const RuntimeMultiplayerMatchSlot& slot) {
                return slot.controller
                    == RuntimeMultiplayerMatchSlotController::Computer;
            }
        )
    );
    const std::size_t combatantCapacity = resumeSave
        ? static_cast<std::size_t>(std::count_if(resumeSetup_->slots.begin(), resumeSetup_->slots.end(),
            [](const auto& slot) { return slot.controller == RuntimeMultiplayerMatchSlotController::Human; }))
        : slotCount - computerCount;
    const std::size_t combinedCapacity = std::min(
        networkOptions.session.maximumMembers,
        runtimeMultiplayerMaximumMembers
    );
    networkOptions.maximumCombatants = std::min(
        {networkOptions.maximumCombatants, combatantCapacity, combinedCapacity}
    );
    networkOptions.maximumSpectators = std::min({
        networkOptions.maximumSpectators,
        runtimeMultiplayerMaximumSpectators,
        combinedCapacity - networkOptions.maximumCombatants
    });
    networkOptions.session.maximumMembers = std::min({
        combinedCapacity,
        networkOptions.maximumCombatants + networkOptions.maximumSpectators,
        runtimeMultiplayerMaximumMembers
    });
    if (!createFreshBridge(errorMessage)) return false;
    if (!bridge_->lan().startHost(std::move(networkOptions), errorMessage)) {
        bridge_.reset();
        return false;
    }
    lobby_ = std::move(lobbyCandidate);
    if (resumeSave && !bridge_->lan().setLobbyResumeSnapshot(resumeSave->descriptor, resumeSave->bytes, errorMessage)) {
        close();return false;
    }
    bool published = false;
    if (!publishHostDraft(published, errorMessage)) {
        close();
        return false;
    }
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerLobbyFlow::startBrowsing(
    std::uint16_t discoveryPort,
    std::string& errorMessage
) {
    close();
    if (!createFreshBridge(errorMessage)) return false;
    if (!bridge_->lan().startBrowsing(discoveryPort, errorMessage)) {
        bridge_.reset();
        return false;
    }
    return true;
}

bool RuntimeMultiplayerLobbyFlow::join(
    RuntimeMultiplayerLanJoinOptions options,
    std::string& errorMessage
) {
    if (!bridge_ && !createFreshBridge(errorMessage)) return false;
    if (bridge_->lan().phase() != RuntimeMultiplayerLanPhase::Inactive
        && bridge_->lan().phase() != RuntimeMultiplayerLanPhase::Browsing) {
        return fail(
            errorMessage,
            "multiplayer lobby flow is already hosting or joining"
        );
    }
    lobby_ = RuntimeMultiplayerLobbyModel{};
    verifiedMapPath_.clear();
    verifiedCanonicalMapId_.clear();
    verifiedMapFingerprint_ = 0;
    verifiedSetupRevision_ = 0;
    return bridge_->lan().join(std::move(options), errorMessage);
}

bool RuntimeMultiplayerLobbyFlow::sendDiscoveryProbe(
    const std::string& targetAddress,
    std::string& errorMessage
) {
    if (!bridge_) {
        return fail(errorMessage, "multiplayer browser is not active");
    }
    return bridge_->lan().sendDiscoveryProbe(targetAddress, errorMessage);
}

std::vector<RuntimeLanRoomAdvertisement>
RuntimeMultiplayerLobbyFlow::discoveredRooms(
    std::uint64_t maximumAgeMilliseconds
) const {
    return bridge_ ? bridge_->lan().discoveredRooms(maximumAgeMilliseconds)
                   : std::vector<RuntimeLanRoomAdvertisement>{};
}

RuntimeMultiplayerLobbyFlowPollResult RuntimeMultiplayerLobbyFlow::poll(
    int transportTimeoutMilliseconds
) {
    RuntimeMultiplayerLobbyFlowPollResult result;
    if (!bridge_) {
        result.succeeded = false;
        result.error = "multiplayer lobby flow is not active";
        return result;
    }
    RuntimeMultiplayerRuntimePumpResult pumped = bridge_->pump(
        false,
        transportTimeoutMilliseconds
    );
    result.events = std::move(pumped.events);
    if (!pumped.succeeded) {
        const bool kicked = bridge_->lan().phase()
                == RuntimeMultiplayerLanPhase::Kicked
            && std::any_of(
                result.events.begin(),
                result.events.end(),
                [](const RuntimeMultiplayerLanEvent& event) {
                    return event.kind
                        == RuntimeMultiplayerLanEventKind::Kicked;
                }
            );
        if (kicked) {
            result.succeeded = true;
            result.error.clear();
            return result;
        }
        result.succeeded = false;
        result.error = std::move(pumped.error);
        return result;
    }

    bool requestChanged = false;
    std::string requestError;
    if (!handleLobbySlotOptionRequests(
            result.events,
            requestChanged,
            requestError
        )) {
        result.succeeded = false;
        result.error = std::move(requestError);
        return result;
    }

    std::string synchronizationError;
    if (!synchronizeLobbyFromSession(
            result.lobbyChanged,
            synchronizationError
        )) {
        result.succeeded = false;
        result.error = std::move(synchronizationError);
        return result;
    }
    result.lobbyChanged = result.lobbyChanged || requestChanged;
    if (bridge_->lan().lobbyResumeDescriptor() && bridge_->lan().lobbyResumeSnapshotReady() && !resumeManifestVerified_) {
        RuntimeMultiplayerResumeSave verified;
        if (!readRuntimeMultiplayerResumeSave(bridge_->lan().lobbyResumeSnapshotBytes(), verified, result.error)
            || verified.descriptor != *bridge_->lan().lobbyResumeDescriptor()
            || verified.compatibility != bridge_->lan().session().config().compatibility) {
            if (result.error.empty()) result.error = "downloaded resume save is incompatible with this room";
            result.succeeded = false;return result;
        }
        resumeSetup_ = std::move(verified.setup);
        resumeManifestVerified_ = true;
        result.lobbyChanged = true;
    }
    result.matchStarted = bridge_->lan().phase()
        == RuntimeMultiplayerLanPhase::Running;
    return result;
}

RuntimeMultiplayerLobbyEditResult
RuntimeMultiplayerLobbyFlow::setMapAndRules(
    const RuntimeMultiplayerMatchMap& map,
    const RuntimeMultiplayerMatchRules& rules,
    std::string& publishError
) {
    if (resuming()) return {false, false, false, false, publishError = "resume room map and rules are fixed by the save"};
    RuntimeMultiplayerLobbyEditResult edit = lobby_.setMapAndRules(map, rules);
    afterHostEdit(edit, publishError);
    return edit;
}

RuntimeMultiplayerLobbyEditResult RuntimeMultiplayerLobbyFlow::setSlot(
    std::size_t slotIndex,
    const RuntimeMultiplayerMatchSlot& slot,
    std::string& publishError
) {
    if (resuming()) return {false, false, false, false, publishError = "resume room slot options are fixed by the save"};
    RuntimeMultiplayerLobbyEditResult edit = lobby_.setSlot(slotIndex, slot);
    afterHostEdit(edit, publishError);
    return edit;
}

RuntimeMultiplayerLobbyEditResult
RuntimeMultiplayerLobbyFlow::setSlotController(
    std::size_t slotIndex,
    RuntimeMultiplayerMatchSlotController controller,
    std::string_view humanPeerId,
    std::string& publishError
) {
    if (resuming()) {
        if (!hostAuthoritative() || !resumeSetup_ || slotIndex >= resumeSetup_->slots.size()
            || controller != RuntimeMultiplayerMatchSlotController::Human
            || resumeSetup_->slots[slotIndex].controller != controller)
            return {false, false, false, false, publishError = "only saved Human seats may be reassigned"};
        auto slots = lobby_.setup().slots;
        auto source = std::find_if(slots.begin(), slots.end(), [&](const auto& slot) { return slot.controller == controller && slot.peerId == humanPeerId; });
        if (source == slots.end()) return {false, false, false, false, publishError = "resume seat assignment requires a joined combatant"};
        const auto sourceIndex = source->slotIndex;
        const std::string displaced = slots[slotIndex].peerId;
        slots[slotIndex] = resumeSetup_->slots[slotIndex];
        slots[slotIndex].peerId = humanPeerId;
        if (sourceIndex != slotIndex) {
            slots[sourceIndex] = displaced.empty() ? RuntimeMultiplayerMatchSlot{} : resumeSetup_->slots[sourceIndex];
            slots[sourceIndex].slotIndex = sourceIndex;
            slots[sourceIndex].peerId = displaced;
        }
        auto edit = lobby_.replaceSlots(slots);
        afterHostEdit(edit, publishError);return edit;
    }
    RuntimeMultiplayerLobbyEditResult edit = lobby_.setSlotController(
        slotIndex,
        controller,
        humanPeerId
    );
    afterHostEdit(edit, publishError);
    return edit;
}

RuntimeMultiplayerLobbyEditResult
RuntimeMultiplayerLobbyFlow::setSlotOptions(
    std::size_t slotIndex,
    std::string countryId,
    std::string colorId,
    std::string teamId,
    std::string difficultyId,
    std::uint8_t startPosition,
    std::string& publishError
) {
    if (resuming()) return {false, false, false, false, publishError = "resume room slot options are fixed by the save"};
    RuntimeMultiplayerLobbyEditResult edit = lobby_.setSlotOptions(
        slotIndex,
        std::move(countryId),
        std::move(colorId),
        std::move(teamId),
        std::move(difficultyId),
        startPosition
    );
    afterHostEdit(edit, publishError);
    return edit;
}

RuntimeMultiplayerLanOperationResult
RuntimeMultiplayerLobbyFlow::requestLocalSlotOptions(
    std::string countryId,
    std::string colorId,
    std::string teamId,
    std::string difficultyId,
    std::uint8_t startPosition
) {
    if (resuming()) return failedOperation("resume room slot options are fixed by the save");
    if (!bridge_ || bridge_->lan().role()
            != RuntimeMultiplayerLanRole::Client
        || bridge_->lan().phase() != RuntimeMultiplayerLanPhase::Lobby
        || !lobby_.initialized()) {
        return failedOperation(
            "only a joined lobby client may request local slot options"
        );
    }
    const std::string& localPeerId = bridge_->lan().localPeerId();
    const auto slot = std::find_if(
        lobby_.setup().slots.begin(),
        lobby_.setup().slots.end(),
        [&](const RuntimeMultiplayerMatchSlot& candidate) {
            return candidate.controller
                    == RuntimeMultiplayerMatchSlotController::Human
                && candidate.peerId == localPeerId;
        }
    );
    if (slot == lobby_.setup().slots.end()) {
        return failedOperation(
            "local client is not bound to a Human lobby slot"
        );
    }
    return bridge_->lan().requestLobbySlotOptions(
        slot->slotIndex,
        std::move(countryId),
        std::move(colorId),
        std::move(teamId),
        std::move(difficultyId),
        startPosition
    );
}

RuntimeMultiplayerLanOperationResult RuntimeMultiplayerLobbyFlow::kickPeer(
    const std::string& peerId,
    std::string reason
) {
    if (!bridge_ || !hostAuthoritative()
        || bridge_->lan().phase() != RuntimeMultiplayerLanPhase::Lobby) {
        return failedOperation("only the multiplayer lobby host may kick");
    }
    RuntimeMultiplayerLanOperationResult result = bridge_->lan().kickPeer(
        peerId,
        std::move(reason)
    );
    if (!result.applied) return result;
    bool changed = false;
    std::string synchronizationError;
    if (!synchronizeLobbyFromSession(changed, synchronizationError)) {
        result.succeeded = false;
        result.reason = std::move(synchronizationError);
    }
    return result;
}

bool RuntimeMultiplayerLobbyFlow::verifyLocalMap(
    const std::filesystem::path& localMapPath,
    std::string& errorMessage
) {
    if (!bridge_ || !lobby_.initialized()) {
        return fail(errorMessage, "canonical multiplayer setup is unavailable");
    }
    RuntimeMultiplayerContentComponent component;
    if (!loadRuntimeMultiplayerContentComponentFile(
            localMapPath,
            lobby_.setup().map.canonicalId,
            component,
            errorMessage
        )) {
        return false;
    }
    std::uint64_t fingerprint = 0;
    if (!runtimeMultiplayerContentFingerprint(
            {component},
            fingerprint,
            errorMessage
        )) {
        return false;
    }
    if (fingerprint != lobby_.setup().map.contentFingerprint) {
        return fail(
            errorMessage,
            "local map content does not match the multiplayer room"
        );
    }
    if (hostAuthoritative() && !bridge_->lan().setLobbyMap(lobby_.setup().map, std::move(component.bytes), errorMessage)) return false;
    verifiedMapPath_ = localMapPath;
    verifiedCanonicalMapId_ = lobby_.setup().map.canonicalId;
    verifiedMapFingerprint_ = fingerprint;
    verifiedSetupRevision_ = lobby_.setup().revision;
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerLobbyFlow::downloadMissingMap(const std::filesystem::path& localDataRoot,
    std::filesystem::path& output, std::string& error) {
    if (!canonicalSetupPublished() || hostAuthoritative()) return fail(error, "只有已加入房间的玩家才能向房主取得地图。");
    auto& lan = bridge_->lan();
    if (!lan.requestLobbyMap(lobby_.setup().map, error)) return false;
    const auto* offer = lan.lobbyMapOffer();
    if (!offer || lan.lobbyMapBytes().empty()) return fail(error, "正在从房主下载地图，校验完成后即可准备……");
    if (offer->request.map != lobby_.setup().map) return fail(error, "房主已换图，正在等待新地图。");
    std::filesystem::path verified;
    if (!storeRuntimeMultiplayerMapCache(localDataRoot, *offer, lan.lobbyMapBytes(), verified, error)
        || !verifyLocalMap(verified, error)) return false;
    output = std::move(verified); error.clear(); return true;
}

bool RuntimeMultiplayerLobbyFlow::localMapVerified() const {
    return lobby_.initialized() && !verifiedMapPath_.empty()
        && verifiedCanonicalMapId_ == lobby_.setup().map.canonicalId
        && verifiedMapFingerprint_ == lobby_.setup().map.contentFingerprint
        && verifiedSetupRevision_ == lobby_.setup().revision;
}

const std::filesystem::path&
RuntimeMultiplayerLobbyFlow::verifiedLocalMapPath() const {
    return verifiedMapPath_;
}

RuntimeMultiplayerLanOperationResult RuntimeMultiplayerLobbyFlow::setReady(
    bool ready
) {
    if (!bridge_) return failedOperation("multiplayer lobby flow is not active");
    std::string resumeError;
    if (ready && !resumeReady(resumeError)) return failedOperation(std::move(resumeError));
    if (ready && (!canonicalSetupPublished() || !localMapVerified())) {
        return failedOperation(
            "ready requires the published room setup and verified local map"
        );
    }
    return bridge_->lan().setReady(ready);
}

RuntimeMultiplayerLanOperationResult RuntimeMultiplayerLobbyFlow::startMatch(
    int startTick
) {
    if (!bridge_ || !hostAuthoritative()) {
        return failedOperation("only the multiplayer lobby host may start");
    }
    std::string startError;
    if (!canStart(startError)) return failedOperation(std::move(startError));
    if (const auto* descriptor = bridge_->lan().lobbyResumeDescriptor()) startTick = descriptor->identity.snapshotTick + 1;
    return bridge_->lan().startMatch(startTick);
}

bool RuntimeMultiplayerLobbyFlow::makeLaunchConfig(
    const RuntimeConfig& baseConfig,
    RuntimeMultiplayerLaunchConfigResult& result,
    std::string& errorMessage
) const {
    if (!bridge_ || bridge_->lan().phase()
            != RuntimeMultiplayerLanPhase::Running) {
        return fail(errorMessage, "multiplayer match has not started");
    }
    if (!localMapVerified()) {
        return fail(errorMessage, "local multiplayer map is not verified");
    }
    if (!resumeReady(errorMessage)) return false;
    if (!bridge_->lan().session().matchSetup().has_value()) {
        return fail(errorMessage, "running multiplayer setup is unavailable");
    }
    return makeRuntimeMultiplayerLaunchConfig(
        *bridge_->lan().session().matchSetup(),
        bridge_->lan().localPeerId(),
        verifiedMapPath_.string(),
        verifiedMapFingerprint_,
        baseConfig,
        result,
        errorMessage,
        bridge_->lan().session().timelineEpoch()
    );
}

bool RuntimeMultiplayerLobbyFlow::initialized() const {
    return bridge_ != nullptr;
}

bool RuntimeMultiplayerLobbyFlow::hostAuthoritative() const {
    return bridge_ && lobby_.hostAuthoritative()
        && bridge_->lan().role() == RuntimeMultiplayerLanRole::Host;
}

bool RuntimeMultiplayerLobbyFlow::canonicalSetupPublished() const {
    return bridge_ && lobby_.initialized()
        && bridge_->lan().session().matchSetup().has_value()
        && *bridge_->lan().session().matchSetup() == lobby_.setup();
}

bool RuntimeMultiplayerLobbyFlow::canStart(
    std::string& errorMessage
) const {
    if (!hostAuthoritative()) {
        return fail(errorMessage, "only the multiplayer lobby host may start");
    }
    if (!canonicalSetupPublished()) {
        return fail(errorMessage, "canonical multiplayer setup is not published");
    }
    if (!localMapVerified()) {
        return fail(errorMessage, "host local map is not verified");
    }
    if (!resumeReady(errorMessage)) return false;
    if (!bridge_->lan().lobbyResumeAllPeersReady()) return fail(errorMessage, "waiting for all players to receive the resume save");
    if (!bridge_->lan().lobbyMapTransfersComplete()) return fail(errorMessage, "waiting for requested map downloads to complete");
    return lobby_.canStart(readyPeerIds(), errorMessage);
}

bool RuntimeMultiplayerLobbyFlow::resuming() const {
    return resumeSetup_.has_value() || (bridge_ && bridge_->lan().lobbyResumeDescriptor());
}

bool RuntimeMultiplayerLobbyFlow::resumeReady(std::string& errorMessage) const {
    if (!resuming()) { errorMessage.clear();return true; }
    if (!bridge_ || !resumeManifestVerified_ || !bridge_->lan().lobbyResumeSnapshotReady())
        return fail(errorMessage, "waiting for the complete resume save");
    const auto* descriptor = bridge_->lan().lobbyResumeDescriptor();
    std::uint64_t fingerprint = 0;
    if (!descriptor || !runtimeMultiplayerGameplaySetupFingerprint(lobby_.setup(), fingerprint, errorMessage)
        || fingerprint != descriptor->identity.gameplaySetupFingerprint)
        return fail(errorMessage, "waiting for all saved Human seats; saved rules and slot options must match");
    errorMessage.clear();return true;
}

const RuntimeMultiplayerLobbyModel& RuntimeMultiplayerLobbyFlow::lobby(
) const {
    return lobby_;
}

RuntimeMultiplayerRuntimeBridge* RuntimeMultiplayerLobbyFlow::bridge() {
    return bridge_.get();
}

const RuntimeMultiplayerRuntimeBridge* RuntimeMultiplayerLobbyFlow::bridge(
) const {
    return bridge_.get();
}

std::unique_ptr<RuntimeMultiplayerRuntimeBridge>
RuntimeMultiplayerLobbyFlow::takeRunningBridge(std::string& errorMessage) {
    if (!bridge_ || bridge_->lan().phase()
            != RuntimeMultiplayerLanPhase::Running) {
        fail(errorMessage, "multiplayer match has not started");
        return nullptr;
    }
    errorMessage.clear();
    lobby_ = RuntimeMultiplayerLobbyModel{};
    verifiedMapPath_.clear();
    verifiedCanonicalMapId_.clear();
    verifiedMapFingerprint_ = 0;
    verifiedSetupRevision_ = 0;
    return std::move(bridge_);
}

void RuntimeMultiplayerLobbyFlow::close() {
    if (bridge_) bridge_->lan().close();
    bridge_.reset();
    lobby_ = RuntimeMultiplayerLobbyModel{};
    verifiedMapPath_.clear();
    verifiedCanonicalMapId_.clear();
    verifiedMapFingerprint_ = 0;
    verifiedSetupRevision_ = 0;
    resumeSetup_.reset();
    resumeManifestVerified_ = false;
}

bool RuntimeMultiplayerLobbyFlow::handleLobbySlotOptionRequests(
    const std::vector<RuntimeMultiplayerLanEvent>& events,
    bool& changed,
    std::string& errorMessage
) {
    changed = false;
    for (const RuntimeMultiplayerLanEvent& event : events) {
        if (event.kind
                != RuntimeMultiplayerLanEventKind::LobbySlotOptionsRequested
            || !event.lobbySlotOptionsRequest.has_value()) {
            continue;
        }
        if (!bridge_ || !hostAuthoritative()
            || bridge_->lan().phase() != RuntimeMultiplayerLanPhase::Lobby) {
            return fail(
                errorMessage,
                "lobby slot request reached a non-authoritative flow"
            );
        }
        const RuntimeMultiplayerLobbySlotOptionsRequestMessage& request =
            *event.lobbySlotOptionsRequest;
        bool accepted = false;
        std::string reason;
        if (resuming()) {
            reason = "resume room slot options are fixed by the save";
        } else if (request.expectedSetupRevision != lobby_.setup().revision) {
            reason = "lobby slot request expected revision is stale";
        } else {
            RuntimeMultiplayerLobbyEditResult edit = lobby_.setSlotOptions(
                request.slotIndex,
                request.countryId,
                request.colorId,
                request.teamId,
                request.difficultyId,
                request.startPosition
            );
            if (!edit.accepted) {
                reason = edit.reason;
            } else {
                std::string publishError;
                if (!afterHostEdit(edit, publishError)) {
                    reason = std::move(publishError);
                } else {
                    accepted = true;
                    changed = changed || edit.changed;
                }
            }
        }
        RuntimeMultiplayerLanOperationResult resolved =
            bridge_->lan().resolveLobbySlotOptionsRequest(
                request.peerId,
                request.requestId,
                accepted,
                std::move(reason)
            );
        if (!resolved.succeeded) {
            return fail(errorMessage, std::move(resolved.reason));
        }
    }
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerLobbyFlow::createFreshBridge(
    std::string& errorMessage
) {
    bridge_ = std::make_unique<RuntimeMultiplayerRuntimeBridge>(
        lockstepConfig_
    );
    if (!bridge_->ready(errorMessage)) {
        bridge_.reset();
        return false;
    }
    return true;
}

bool RuntimeMultiplayerLobbyFlow::synchronizeLobbyFromSession(
    bool& changed,
    std::string& errorMessage
) {
    changed = false;
    if (!bridge_) return fail(errorMessage, "multiplayer lobby is inactive");
    const RuntimeMultiplayerLanPhase phase = bridge_->lan().phase();
    if (phase != RuntimeMultiplayerLanPhase::Lobby
        && phase != RuntimeMultiplayerLanPhase::Running) {
        errorMessage.clear();
        return true;
    }

    if (hostAuthoritative()) {
        const RuntimeMultiplayerLobbyEditResult synchronized =
            resumeSetup_ ? lobby_.syncResumeMembers(bridge_->lan().session().members(), *resumeSetup_)
                         : lobby_.syncMembers(bridge_->lan().session().members());
        if (!synchronized.accepted) {
            return fail(errorMessage, synchronized.reason);
        }
        changed = synchronized.changed;
        if (synchronized.changed) {
            invalidateMapVerificationIfChanged();
            bool published = false;
            if (!publishHostDraft(published, errorMessage)) return false;
        }
        errorMessage.clear();
        return true;
    }

    if (!bridge_->lan().session().matchSetup().has_value()) {
        errorMessage.clear();
        return true;
    }
    const RuntimeMultiplayerMatchSetup& setup =
        *bridge_->lan().session().matchSetup();
    RuntimeMultiplayerLobbyEditResult replacement;
    if (!lobby_.initialized()) {
        RuntimeMultiplayerLobbyModel created;
        if (!RuntimeMultiplayerLobbyModel::createClient(
                setup,
                created,
                errorMessage
            )) {
            return false;
        }
        lobby_ = std::move(created);
        changed = true;
    } else {
        replacement = lobby_.replaceCanonicalSetup(setup);
        if (!replacement.accepted) {
            return fail(errorMessage, replacement.reason);
        }
        changed = replacement.changed;
    }
    if (changed) invalidateMapVerificationIfChanged();
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerLobbyFlow::publishHostDraft(
    bool& published,
    std::string& errorMessage
) {
    published = false;
    if (!bridge_ || !lobby_.hostAuthoritative()) {
        return fail(errorMessage, "multiplayer lobby host is unavailable");
    }
    if (canonicalSetupPublished()) {
        published = true;
        errorMessage.clear();
        return true;
    }
    std::string validationError;
    if (!validateRuntimeMultiplayerMatchSetup(
            lobby_.setup(),
            validationError
        )) {
        if (expectedIncompleteDraftError(validationError)) {
            errorMessage.clear();
            return true;
        }
        return fail(
            errorMessage,
            "multiplayer lobby draft is invalid: " + validationError
        );
    }
    const RuntimeMultiplayerLanOperationResult operation =
        bridge_->lan().setMatchSetup(lobby_.setup());
    if (!operation.succeeded) return fail(errorMessage, operation.reason);
    published = operation.applied || operation.duplicate;
    errorMessage.clear();
    return true;
}

bool RuntimeMultiplayerLobbyFlow::afterHostEdit(
    const RuntimeMultiplayerLobbyEditResult& edit,
    std::string& publishError
) {
    if (!edit.accepted || !edit.changed) {
        publishError = edit.accepted ? std::string{} : edit.reason;
        return edit.accepted;
    }
    invalidateMapVerificationIfChanged();
    bool published = false;
    return publishHostDraft(published, publishError);
}

void RuntimeMultiplayerLobbyFlow::invalidateMapVerificationIfChanged() {
    if (!lobby_.initialized()
        || verifiedCanonicalMapId_ != lobby_.setup().map.canonicalId
        || verifiedMapFingerprint_ != lobby_.setup().map.contentFingerprint
        || verifiedSetupRevision_ != lobby_.setup().revision) {
        verifiedMapPath_.clear();
        verifiedCanonicalMapId_.clear();
        verifiedMapFingerprint_ = 0;
        verifiedSetupRevision_ = 0;
        if (bridge_ && bridge_->lan().phase()
                == RuntimeMultiplayerLanPhase::Lobby) {
            const auto local = std::find_if(
                bridge_->lan().session().members().begin(),
                bridge_->lan().session().members().end(),
                [this](const RuntimeMultiplayerRoomMember& member) {
                    return member.peerId == bridge_->lan().localPeerId();
                }
            );
            if (local != bridge_->lan().session().members().end()
                && local->ready) {
                bridge_->lan().setReady(false);
            }
        }
    }
}

std::vector<std::string> RuntimeMultiplayerLobbyFlow::readyPeerIds() const {
    std::vector<std::string> ready;
    if (!bridge_) return ready;
    for (const RuntimeMultiplayerRoomMember& member
         : bridge_->lan().session().members()) {
        if (member.ready) ready.push_back(member.peerId);
    }
    std::sort(ready.begin(), ready.end());
    return ready;
}
