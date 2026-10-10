#include "network/lobby/RuntimeMultiplayerLobbyModel.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

RuntimeMultiplayerLobbyEditResult rejected(std::string reason) {
    RuntimeMultiplayerLobbyEditResult result;
    result.reason = std::move(reason);
    return result;
}

RuntimeMultiplayerLobbyEditResult acceptedUnchanged() {
    RuntimeMultiplayerLobbyEditResult result;
    result.accepted = true;
    return result;
}

RuntimeMultiplayerMatchSlot closedSlot(std::uint8_t slotIndex) {
    RuntimeMultiplayerMatchSlot slot;
    slot.slotIndex = slotIndex;
    slot.controller = RuntimeMultiplayerMatchSlotController::Closed;
    return slot;
}

RuntimeMultiplayerMatchSlot activeSlot(
    std::uint8_t slotIndex,
    RuntimeMultiplayerMatchSlotController controller,
    std::string peerId = {},
    const RuntimeMultiplayerMatchRules* rules = nullptr
) {
    RuntimeMultiplayerMatchSlot slot;
    slot.slotIndex = slotIndex;
    slot.controller = controller;
    slot.peerId = std::move(peerId);
    slot.countryId = "random";
    slot.colorId = "random";
    slot.teamId = "none";
    slot.difficultyId = "medium";
    if (rules && rules->cooperativeMission.scenarioFingerprint != 0) {
        const auto& countries = slotIndex < 2 ? rules->cooperativeMission.playerCountries
                                               : rules->cooperativeMission.enemyCountries;
        if (!countries.empty()) slot.countryId = countries.front();
        slot.teamId = slotIndex < 2 ? "1" : "2";
        slot.startPosition = slotIndex + 1;
    }
    return slot;
}

bool isCleanClosed(const RuntimeMultiplayerMatchSlot& slot) {
    return slot.controller == RuntimeMultiplayerMatchSlotController::Closed
        && slot.peerId.empty()
        && slot.countryId.empty()
        && slot.colorId.empty()
        && slot.teamId.empty()
        && slot.difficultyId.empty()
        && slot.startPosition == 0U;
}

// MatchSetup's launch validator intentionally requires two active slots. A
// lobby draft must also represent the waiting-room state before a second
// combatant joins. Validate that draft through the same canonical validator by
// temporarily filling only clean Closed slots, then restore them. No map,
// rules, member, active-slot, uniqueness, or cross-reference rule is copied.
bool canonicalizeLobbyDraft(
    RuntimeMultiplayerMatchSetup& setup,
    std::string& errorMessage
) {
    RuntimeMultiplayerMatchSetup probe = setup;
    std::size_t activeCount = 0U;
    for (const RuntimeMultiplayerMatchSlot& slot : probe.slots) {
        if (slot.controller != RuntimeMultiplayerMatchSlotController::Closed) {
            ++activeCount;
        }
    }

    std::vector<std::uint8_t> temporaryComputerSlots;
    for (RuntimeMultiplayerMatchSlot& slot : probe.slots) {
        if (activeCount >= 2U) break;
        if (!isCleanClosed(slot)) continue;
        temporaryComputerSlots.push_back(slot.slotIndex);
        slot = activeSlot(
            slot.slotIndex,
            RuntimeMultiplayerMatchSlotController::Computer
        );
        ++activeCount;
    }

    if (!canonicalizeRuntimeMultiplayerMatchSetup(probe, errorMessage)) {
        return false;
    }
    for (const std::uint8_t slotIndex : temporaryComputerSlots) {
        const auto slot = std::find_if(
            probe.slots.begin(),
            probe.slots.end(),
            [slotIndex](const RuntimeMultiplayerMatchSlot& candidate) {
                return candidate.slotIndex == slotIndex;
            }
        );
        if (slot == probe.slots.end()) {
            errorMessage = "temporary lobby slot disappeared";
            return false;
        }
        *slot = closedSlot(slotIndex);
    }
    setup = std::move(probe);
    errorMessage.clear();
    return true;
}

bool validateCanonicalLobbyDraft(
    const RuntimeMultiplayerMatchSetup& setup,
    std::string& errorMessage
) {
    RuntimeMultiplayerMatchSetup canonical = setup;
    if (!canonicalizeLobbyDraft(canonical, errorMessage)) return false;
    if (canonical != setup) {
        errorMessage = "lobby setup is not in canonical draft form";
        return false;
    }
    errorMessage.clear();
    return true;
}

bool sameGameplay(
    const RuntimeMultiplayerMatchSetup& left,
    const RuntimeMultiplayerMatchSetup& right
) {
    if (left.map != right.map
        || left.rules != right.rules
        || left.slots.size() != right.slots.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.slots.size(); ++index) {
        const RuntimeMultiplayerMatchSlot& leftSlot = left.slots[index];
        const RuntimeMultiplayerMatchSlot& rightSlot = right.slots[index];
        if (leftSlot.slotIndex != rightSlot.slotIndex
            || leftSlot.controller != rightSlot.controller
            || leftSlot.countryId != rightSlot.countryId
            || leftSlot.colorId != rightSlot.colorId
            || leftSlot.teamId != rightSlot.teamId
            || leftSlot.difficultyId != rightSlot.difficultyId
            || leftSlot.startPosition != rightSlot.startPosition) {
            return false;
        }
    }
    return true;
}

bool sameRoster(
    const RuntimeMultiplayerMatchSetup& left,
    const RuntimeMultiplayerMatchSetup& right
) {
    if (left.hostPeerId != right.hostPeerId
        || left.members != right.members
        || left.slots.size() != right.slots.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.slots.size(); ++index) {
        if (left.slots[index].peerId != right.slots[index].peerId) {
            return false;
        }
    }
    return true;
}

bool validRoomRole(RuntimeMultiplayerRoomMemberRole role) {
    switch (role) {
    case RuntimeMultiplayerRoomMemberRole::Host:
    case RuntimeMultiplayerRoomMemberRole::Client:
        return true;
    }
    return false;
}

bool validParticipation(RuntimeMultiplayerParticipationRole role) {
    switch (role) {
    case RuntimeMultiplayerParticipationRole::Combatant:
    case RuntimeMultiplayerParticipationRole::Spectator:
        return true;
    }
    return false;
}

RuntimeMultiplayerMatchMemberIdentity matchIdentity(
    RuntimeMultiplayerParticipationRole participation
) {
    return participation == RuntimeMultiplayerParticipationRole::Spectator
        ? RuntimeMultiplayerMatchMemberIdentity::Spectator
        : RuntimeMultiplayerMatchMemberIdentity::Combatant;
}

const RuntimeMultiplayerMatchMember* findMatchMember(
    const std::vector<RuntimeMultiplayerMatchMember>& members,
    std::string_view peerId
) {
    const auto member = std::lower_bound(
        members.begin(),
        members.end(),
        peerId,
        [](const RuntimeMultiplayerMatchMember& candidate,
           std::string_view value) {
            return candidate.peerId < value;
        }
    );
    return member != members.end() && member->peerId == peerId
        ? &*member
        : nullptr;
}

bool hasHumanSlot(
    const RuntimeMultiplayerMatchSetup& setup,
    std::string_view peerId
) {
    return std::any_of(
        setup.slots.begin(),
        setup.slots.end(),
        [peerId](const RuntimeMultiplayerMatchSlot& slot) {
            return slot.controller
                    == RuntimeMultiplayerMatchSlotController::Human
                && slot.peerId == peerId;
        }
    );
}

} // namespace

bool RuntimeMultiplayerLobbyModel::createHost(
    const RuntimeMultiplayerLobbyHostCreateOptions& options,
    RuntimeMultiplayerLobbyModel& model,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (options.slotCount < 2U
        || options.slotCount > runtimeMultiplayerMatchMaximumSlots) {
        errorMessage = "lobby slot count must be in the range 2..8";
        return false;
    }
    if (options.hostMember.role != RuntimeMultiplayerRoomMemberRole::Host) {
        errorMessage = "lobby host member must have the Host room role";
        return false;
    }
    if (options.hostMember.participation
        != RuntimeMultiplayerParticipationRole::Combatant) {
        errorMessage = "new lobby host must initially be a combatant";
        return false;
    }

    std::set<std::size_t> computerSlots;
    for (const std::size_t slotIndex : options.computerSlotIndices) {
        if (slotIndex == 0U || slotIndex >= options.slotCount) {
            errorMessage = "initial computer slot index is out of range";
            return false;
        }
        if (!computerSlots.insert(slotIndex).second) {
            errorMessage = "initial computer slot index is duplicated";
            return false;
        }
    }

    RuntimeMultiplayerMatchSetup setup;
    setup.revision = 1U;
    setup.hostPeerId = options.hostMember.peerId;
    setup.map = options.map;
    setup.rules = options.rules;
    RuntimeMultiplayerMatchMember host;
    host.peerId = options.hostMember.peerId;
    host.displayName = options.hostMember.displayName;
    host.identity = RuntimeMultiplayerMatchMemberIdentity::Combatant;
    setup.members.push_back(std::move(host));
    setup.slots.reserve(options.slotCount);
    setup.slots.push_back(activeSlot(
        0U,
        RuntimeMultiplayerMatchSlotController::Human,
        options.hostMember.peerId,
        &setup.rules
    ));
    for (std::size_t index = 1U; index < options.slotCount; ++index) {
        const auto slotIndex = static_cast<std::uint8_t>(index);
        setup.slots.push_back(computerSlots.contains(index)
            ? activeSlot(
                slotIndex,
                RuntimeMultiplayerMatchSlotController::Computer,
                {}, &setup.rules
            )
            : closedSlot(slotIndex));
    }
    if (!canonicalizeLobbyDraft(setup, errorMessage)) return false;

    RuntimeMultiplayerLobbyModel candidate;
    candidate.authority_ = RuntimeMultiplayerLobbyAuthority::Host;
    candidate.setup_ = std::move(setup);
    model = std::move(candidate);
    return true;
}

bool RuntimeMultiplayerLobbyModel::restoreHost(
    const RuntimeMultiplayerMatchSetup& canonicalSetup,
    RuntimeMultiplayerLobbyModel& model,
    std::string& errorMessage
) {
    if (!validateCanonicalLobbyDraft(
            canonicalSetup,
            errorMessage
        )) {
        return false;
    }
    RuntimeMultiplayerLobbyModel candidate;
    candidate.authority_ = RuntimeMultiplayerLobbyAuthority::Host;
    candidate.setup_ = canonicalSetup;
    model = std::move(candidate);
    return true;
}

bool RuntimeMultiplayerLobbyModel::createClient(
    const RuntimeMultiplayerMatchSetup& canonicalSetup,
    RuntimeMultiplayerLobbyModel& model,
    std::string& errorMessage
) {
    if (!validateCanonicalLobbyDraft(
            canonicalSetup,
            errorMessage
        )) {
        return false;
    }
    RuntimeMultiplayerLobbyModel candidate;
    candidate.authority_ = RuntimeMultiplayerLobbyAuthority::Client;
    candidate.setup_ = canonicalSetup;
    model = std::move(candidate);
    return true;
}

RuntimeMultiplayerLobbyAuthority RuntimeMultiplayerLobbyModel::authority(
) const {
    return authority_;
}

bool RuntimeMultiplayerLobbyModel::initialized() const {
    return authority_ != RuntimeMultiplayerLobbyAuthority::Uninitialized;
}

bool RuntimeMultiplayerLobbyModel::hostAuthoritative() const {
    return authority_ == RuntimeMultiplayerLobbyAuthority::Host;
}

const RuntimeMultiplayerMatchSetup& RuntimeMultiplayerLobbyModel::setup(
) const {
    return setup_;
}

RuntimeMultiplayerLobbyEditResult
RuntimeMultiplayerLobbyModel::commitHostCandidate(
    RuntimeMultiplayerMatchSetup candidate
) {
    if (!hostAuthoritative()) {
        return rejected("only the authoritative host may edit the lobby");
    }
    candidate.revision = setup_.revision;
    std::string errorMessage;
    if (!canonicalizeLobbyDraft(candidate, errorMessage)) {
        return rejected(std::move(errorMessage));
    }
    if (candidate == setup_) return acceptedUnchanged();
    if (setup_.revision == std::numeric_limits<std::uint64_t>::max()) {
        return rejected("lobby setup revision overflow");
    }

    RuntimeMultiplayerLobbyEditResult result;
    result.accepted = true;
    result.changed = true;
    result.gameplayChanged = !sameGameplay(setup_, candidate);
    result.rosterChanged = !sameRoster(setup_, candidate);
    candidate.revision = setup_.revision + 1U;
    setup_ = std::move(candidate);
    return result;
}

bool RuntimeMultiplayerLobbyModel::createResumeHost(const RuntimeMultiplayerMatchSetup& saved,
    const RuntimeMultiplayerRoomMember& host, RuntimeMultiplayerLobbyModel& model, std::string& errorMessage) {
    if (!validateRuntimeMultiplayerMatchSetup(saved, errorMessage)) return false;
    RuntimeMultiplayerLobbyModel candidate;
    candidate.authority_ = RuntimeMultiplayerLobbyAuthority::Host;
    candidate.setup_ = saved;
    candidate.setup_.revision = 1;
    candidate.setup_.hostPeerId = host.peerId;
    // Bind a newly generated LAN identity to the original host's seat.
    for (auto& slot : candidate.setup_.slots) {
        if (slot.controller == RuntimeMultiplayerMatchSlotController::Human)
            slot.peerId = slot.peerId == saved.hostPeerId ? host.peerId : std::string{};
    }
    const auto edit = candidate.syncResumeMembers(std::span(&host, 1), saved);
    if (!edit.accepted) { errorMessage = edit.reason;return false; }
    model = std::move(candidate);errorMessage.clear();return true;
}

RuntimeMultiplayerLobbyEditResult RuntimeMultiplayerLobbyModel::syncResumeMembers(
    std::span<const RuntimeMultiplayerRoomMember> members, const RuntimeMultiplayerMatchSetup& saved) {
    if (!hostAuthoritative()) return rejected("only the host may restore saved seats");
    RuntimeMultiplayerMatchSetup candidate = saved;
    candidate.hostPeerId = setup_.hostPeerId;
    candidate.members.clear();
    std::size_t hosts = 0;
    for (const auto& member : members) {
        if (!validRoomRole(member.role) || !validParticipation(member.participation))
            return rejected("invalid resume room member");
        if (member.role == RuntimeMultiplayerRoomMemberRole::Host) {
            ++hosts;
            if (member.peerId != candidate.hostPeerId) return rejected("resume host identity changed");
        }
        candidate.members.push_back({member.peerId, member.displayName, matchIdentity(member.participation)});
    }
    if (hosts != 1) return rejected("resume room requires exactly one host");
    std::sort(candidate.members.begin(), candidate.members.end(), [](const auto& a, const auto& b) { return a.peerId < b.peerId; });
    for (auto& slot : candidate.slots)
        if (slot.controller == RuntimeMultiplayerMatchSlotController::Human) slot = closedSlot(slot.slotIndex);
    const auto assign = [&](std::size_t index, const RuntimeMultiplayerMatchMember& member) {
        if (index >= saved.slots.size() || saved.slots[index].controller != RuntimeMultiplayerMatchSlotController::Human
            || candidate.slots[index].controller != RuntimeMultiplayerMatchSlotController::Closed
            || member.identity != RuntimeMultiplayerMatchMemberIdentity::Combatant
            || hasHumanSlot(candidate, member.peerId)) return false;
        candidate.slots[index] = saved.slots[index];
        candidate.slots[index].peerId = member.peerId;return true;
    };
    for (const auto& slot : setup_.slots) {
        if (slot.controller != RuntimeMultiplayerMatchSlotController::Human) continue;
        const auto* member = findMatchMember(candidate.members, slot.peerId);
        if (member) assign(slot.slotIndex, *member);
    }
    for (const auto& member : candidate.members) {
        if (member.identity != RuntimeMultiplayerMatchMemberIdentity::Combatant || hasHumanSlot(candidate, member.peerId)) continue;
        for (const auto& slot : saved.slots) {
            if (slot.peerId == member.peerId && assign(slot.slotIndex, member)) break;
        }
        if (hasHumanSlot(candidate, member.peerId)) continue;
        const auto names = std::count_if(saved.members.begin(), saved.members.end(), [&](const auto& old) {
            return old.identity == RuntimeMultiplayerMatchMemberIdentity::Combatant && old.displayName == member.displayName;
        });
        if (names == 1) for (const auto& slot : saved.slots) {
            const auto* old = findMatchMember(saved.members, slot.peerId);
            if (old && old->displayName == member.displayName && assign(slot.slotIndex, member)) break;
        }
    }
    for (const auto& member : candidate.members) {
        if (member.identity != RuntimeMultiplayerMatchMemberIdentity::Combatant || hasHumanSlot(candidate, member.peerId)) continue;
        bool placed = false;
        for (std::size_t index = 0; index < saved.slots.size(); ++index) {
            if (assign(index, member)) { placed = true;break; }
        }
        if (!placed) return rejected("all saved Human seats are occupied");
    }
    return commitHostCandidate(std::move(candidate));
}

RuntimeMultiplayerLobbyEditResult RuntimeMultiplayerLobbyModel::syncMembers(
    std::span<const RuntimeMultiplayerRoomMember> authoritativeMembers
) {
    if (!hostAuthoritative()) {
        return rejected("only the authoritative host may synchronize members");
    }
    if (authoritativeMembers.empty()
        || authoritativeMembers.size()
            > runtimeMultiplayerMatchMaximumMembers) {
        return rejected("authoritative lobby member count is invalid");
    }

    std::vector<RuntimeMultiplayerRoomMember> members(
        authoritativeMembers.begin(),
        authoritativeMembers.end()
    );
    std::sort(
        members.begin(),
        members.end(),
        [](const RuntimeMultiplayerRoomMember& left,
           const RuntimeMultiplayerRoomMember& right) {
            return left.peerId < right.peerId;
        }
    );
    std::size_t hostCount = 0U;
    std::string previousPeerId;
    bool havePreviousPeerId = false;
    for (const RuntimeMultiplayerRoomMember& member : members) {
        if (!validRoomRole(member.role)) {
            return rejected("authoritative member room role is invalid");
        }
        if (!validParticipation(member.participation)) {
            return rejected("authoritative member participation is invalid");
        }
        if (havePreviousPeerId && member.peerId == previousPeerId) {
            return rejected("authoritative member peer id is duplicated");
        }
        previousPeerId = member.peerId;
        havePreviousPeerId = true;
        if (member.role == RuntimeMultiplayerRoomMemberRole::Host) {
            ++hostCount;
            if (member.peerId != setup_.hostPeerId) {
                return rejected("authoritative room host does not match lobby host");
            }
        } else if (member.peerId == setup_.hostPeerId) {
            return rejected("lobby host lost the authoritative Host room role");
        }
    }
    if (hostCount != 1U) {
        return rejected("authoritative room must contain exactly one host");
    }

    RuntimeMultiplayerMatchSetup candidate = setup_;
    candidate.members.clear();
    candidate.members.reserve(members.size());
    for (const RuntimeMultiplayerRoomMember& member : members) {
        RuntimeMultiplayerMatchMember matchMember;
        matchMember.peerId = member.peerId;
        matchMember.displayName = member.displayName;
        matchMember.identity = matchIdentity(member.participation);
        candidate.members.push_back(std::move(matchMember));
    }

    for (RuntimeMultiplayerMatchSlot& slot : candidate.slots) {
        if (slot.controller != RuntimeMultiplayerMatchSlotController::Human) {
            continue;
        }
        const RuntimeMultiplayerMatchMember* member = findMatchMember(
            candidate.members,
            slot.peerId
        );
        if (member == nullptr
            || member->identity
                == RuntimeMultiplayerMatchMemberIdentity::Spectator) {
            slot = closedSlot(slot.slotIndex);
        }
    }

    for (const RuntimeMultiplayerMatchMember& member : candidate.members) {
        if (member.identity
                != RuntimeMultiplayerMatchMemberIdentity::Combatant
            || hasHumanSlot(candidate, member.peerId)) {
            continue;
        }
        const auto freeSlot = std::find_if(
            candidate.slots.begin(),
            candidate.slots.end(),
            [](const RuntimeMultiplayerMatchSlot& slot) {
                return slot.controller
                    == RuntimeMultiplayerMatchSlotController::Closed;
            }
        );
        if (freeSlot == candidate.slots.end()) {
            return rejected("no Closed lobby slot is available for combatant");
        }
        *freeSlot = activeSlot(
            freeSlot->slotIndex,
            RuntimeMultiplayerMatchSlotController::Human,
            member.peerId,
            &candidate.rules
        );
    }
    return commitHostCandidate(std::move(candidate));
}

RuntimeMultiplayerLobbyEditResult RuntimeMultiplayerLobbyModel::setMap(
    const RuntimeMultiplayerMatchMap& map
) {
    RuntimeMultiplayerMatchSetup candidate = setup_;
    candidate.map = map;
    return commitHostCandidate(std::move(candidate));
}

RuntimeMultiplayerLobbyEditResult RuntimeMultiplayerLobbyModel::setRules(
    const RuntimeMultiplayerMatchRules& rules
) {
    RuntimeMultiplayerMatchSetup candidate = setup_;
    candidate.rules = rules;
    return commitHostCandidate(std::move(candidate));
}

RuntimeMultiplayerLobbyEditResult
RuntimeMultiplayerLobbyModel::setMapAndRules(
    const RuntimeMultiplayerMatchMap& map,
    const RuntimeMultiplayerMatchRules& rules
) {
    RuntimeMultiplayerMatchSetup candidate = setup_;
    candidate.map = map;
    candidate.rules = rules;
    return commitHostCandidate(std::move(candidate));
}

RuntimeMultiplayerLobbyEditResult
RuntimeMultiplayerLobbyModel::setSlotController(
    std::size_t slotIndex,
    RuntimeMultiplayerMatchSlotController controller,
    std::string_view humanPeerId
) {
    if (!hostAuthoritative()) {
        return rejected("only the authoritative host may edit lobby slots");
    }
    if (slotIndex >= setup_.slots.size()) {
        return rejected("lobby slot index is out of range");
    }
    RuntimeMultiplayerMatchSetup candidate = setup_;
    RuntimeMultiplayerMatchSlot& slot = candidate.slots[slotIndex];
    switch (controller) {
    case RuntimeMultiplayerMatchSlotController::Closed:
        if (!humanPeerId.empty()) {
            return rejected("Closed lobby slot cannot bind a human peer");
        }
        slot = closedSlot(slot.slotIndex);
        break;
    case RuntimeMultiplayerMatchSlotController::Computer:
        if (!humanPeerId.empty()) {
            return rejected("Computer lobby slot cannot bind a human peer");
        }
        if (slot.controller == RuntimeMultiplayerMatchSlotController::Closed) {
            slot = activeSlot(
                slot.slotIndex,
                RuntimeMultiplayerMatchSlotController::Computer,
                {}, &candidate.rules
            );
        } else {
            slot.controller = RuntimeMultiplayerMatchSlotController::Computer;
            slot.peerId.clear();
        }
        break;
    case RuntimeMultiplayerMatchSlotController::Human: {
        const std::string peerId = humanPeerId.empty()
            ? slot.peerId
            : std::string(humanPeerId);
        if (slot.controller == RuntimeMultiplayerMatchSlotController::Closed) {
            slot = activeSlot(
                slot.slotIndex,
                RuntimeMultiplayerMatchSlotController::Human,
                peerId,
                &candidate.rules
            );
        } else {
            slot.controller = RuntimeMultiplayerMatchSlotController::Human;
            slot.peerId = peerId;
        }
        break;
    }
    default:
        return rejected("lobby slot controller is invalid");
    }
    return commitHostCandidate(std::move(candidate));
}

RuntimeMultiplayerLobbyEditResult
RuntimeMultiplayerLobbyModel::setSlotOptions(
    std::size_t slotIndex,
    std::string countryId,
    std::string colorId,
    std::string teamId,
    std::string difficultyId,
    std::uint8_t startPosition
) {
    if (!hostAuthoritative()) {
        return rejected("only the authoritative host may edit lobby slots");
    }
    if (slotIndex >= setup_.slots.size()) {
        return rejected("lobby slot index is out of range");
    }
    RuntimeMultiplayerMatchSetup candidate = setup_;
    RuntimeMultiplayerMatchSlot& slot = candidate.slots[slotIndex];
    slot.countryId = std::move(countryId);
    slot.colorId = std::move(colorId);
    slot.teamId = std::move(teamId);
    slot.difficultyId = std::move(difficultyId);
    slot.startPosition = startPosition;
    return commitHostCandidate(std::move(candidate));
}

RuntimeMultiplayerLobbyEditResult RuntimeMultiplayerLobbyModel::setSlot(
    std::size_t slotIndex,
    const RuntimeMultiplayerMatchSlot& slot
) {
    if (!hostAuthoritative()) {
        return rejected("only the authoritative host may edit lobby slots");
    }
    if (slotIndex >= setup_.slots.size()) {
        return rejected("lobby slot index is out of range");
    }
    if (slot.slotIndex != static_cast<std::uint8_t>(slotIndex)) {
        return rejected("replacement lobby slot index does not match target");
    }
    RuntimeMultiplayerMatchSetup candidate = setup_;
    candidate.slots[slotIndex] = slot;
    return commitHostCandidate(std::move(candidate));
}

RuntimeMultiplayerLobbyEditResult RuntimeMultiplayerLobbyModel::replaceSlots(
    std::span<const RuntimeMultiplayerMatchSlot> slots
) {
    if (!hostAuthoritative()) {
        return rejected("only the authoritative host may replace lobby slots");
    }
    if (slots.size() != setup_.slots.size()) {
        return rejected("replacement lobby slots changed fixed capacity");
    }
    RuntimeMultiplayerMatchSetup candidate = setup_;
    candidate.slots.assign(slots.begin(), slots.end());
    return commitHostCandidate(std::move(candidate));
}

RuntimeMultiplayerLobbyEditResult
RuntimeMultiplayerLobbyModel::replaceCanonicalSetup(
    const RuntimeMultiplayerMatchSetup& canonicalSetup
) {
    if (authority_ != RuntimeMultiplayerLobbyAuthority::Client) {
        return rejected("only a client lobby accepts canonical replacement");
    }
    std::string errorMessage;
    if (!validateCanonicalLobbyDraft(
            canonicalSetup,
            errorMessage
        )) {
        return rejected(std::move(errorMessage));
    }
    if (canonicalSetup.hostPeerId != setup_.hostPeerId) {
        return rejected("canonical lobby replacement changed the room host");
    }
    if (canonicalSetup.slots.size() != setup_.slots.size()) {
        return rejected("canonical lobby replacement changed fixed capacity");
    }
    if (canonicalSetup.revision < setup_.revision) {
        return rejected("canonical lobby replacement rolled revision back");
    }
    if (canonicalSetup.revision == setup_.revision) {
        return canonicalSetup == setup_
            ? acceptedUnchanged()
            : rejected("canonical lobby replacement conflicts at same revision");
    }

    RuntimeMultiplayerLobbyEditResult result;
    result.accepted = true;
    result.changed = true;
    result.gameplayChanged = !sameGameplay(setup_, canonicalSetup);
    result.rosterChanged = !sameRoster(setup_, canonicalSetup);
    setup_ = canonicalSetup;
    return result;
}

bool RuntimeMultiplayerLobbyModel::canStart(
    std::span<const std::string> readyPeerIds,
    std::string& errorMessage
) const {
    errorMessage.clear();
    if (!initialized()) {
        errorMessage = "lobby model is not initialized";
        return false;
    }
    if (!validateRuntimeMultiplayerMatchSetup(setup_, errorMessage)) {
        return false;
    }
    const std::size_t combatantCount = static_cast<std::size_t>(std::count_if(
        setup_.members.begin(),
        setup_.members.end(),
        [](const RuntimeMultiplayerMatchMember& member) {
            return member.identity
                == RuntimeMultiplayerMatchMemberIdentity::Combatant;
        }
    ));
    if (combatantCount < 2U) {
        errorMessage = "lobby requires at least two player combatants";
        return false;
    }
    if (readyPeerIds.size() != setup_.members.size()) {
        errorMessage = "every lobby member must be ready";
        return false;
    }
    std::set<std::string> readyPeers;
    for (const std::string& peerId : readyPeerIds) {
        if (!readyPeers.insert(peerId).second) {
            errorMessage = "ready peer id is duplicated";
            return false;
        }
    }
    for (const std::string& peerId : readyPeers) {
        if (findMatchMember(setup_.members, peerId) == nullptr) {
            errorMessage = "ready peer is not a lobby member";
            return false;
        }
    }
    for (const RuntimeMultiplayerMatchMember& member : setup_.members) {
        if (!readyPeers.contains(member.peerId)) {
            errorMessage = "every lobby member must be ready";
            return false;
        }
    }
    return true;
}
