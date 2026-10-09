#pragma once

#include "RuntimeMultiplayerSessionSystem.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

enum class RuntimeMultiplayerLobbyAuthority : std::uint8_t {
    Uninitialized,
    Host,
    Client
};

struct RuntimeMultiplayerLobbyEditResult {
    bool accepted = false;
    bool changed = false;
    // Gameplay changes invalidate any ready decision made for the previous
    // map/rules/slot configuration.
    bool gameplayChanged = false;
    // Roster changes include display-name-only updates and human-slot peer
    // rebinding. Ready state deliberately remains owned by SessionSystem.
    bool rosterChanged = false;
    std::string reason;

    bool operator==(const RuntimeMultiplayerLobbyEditResult&) const = default;
};

struct RuntimeMultiplayerLobbyHostCreateOptions {
    RuntimeMultiplayerMatchMap map;
    RuntimeMultiplayerMatchRules rules;
    RuntimeMultiplayerRoomMember hostMember;
    std::size_t slotCount = 8U;
    // Every non-host slot starts Closed unless its zero-based index is listed
    // here. Slot zero is always the host's Human slot.
    std::vector<std::size_t> computerSlotIndices;

    bool operator==(
        const RuntimeMultiplayerLobbyHostCreateOptions&
    ) const = default;
};

// Pure-value editor for LAN and Internet lobby frontends. It owns no socket,
// ready flag, local path, or UI state. Host edits are transactional and every
// material change advances the authoritative setup revision exactly once.
class RuntimeMultiplayerLobbyModel {
public:
    RuntimeMultiplayerLobbyModel() = default;

    // Creates revision 1. The host is a Combatant in Human slot zero; the
    // remaining fixed-capacity slots are Closed or explicitly Computer.
    static bool createHost(
        const RuntimeMultiplayerLobbyHostCreateOptions& options,
        RuntimeMultiplayerLobbyModel& model,
        std::string& errorMessage
    );

    // Restores an already-canonical lobby draft, for example after a frontend
    // reconstruction. Unlike createHost, its revision is preserved. A draft
    // may still be waiting for the second active slot required by canStart.
    static bool restoreHost(
        const RuntimeMultiplayerMatchSetup& canonicalSetup,
        RuntimeMultiplayerLobbyModel& model,
        std::string& errorMessage
    );

    static bool createClient(
        const RuntimeMultiplayerMatchSetup& canonicalSetup,
        RuntimeMultiplayerLobbyModel& model,
        std::string& errorMessage
    );

    RuntimeMultiplayerLobbyAuthority authority() const;
    bool initialized() const;
    bool hostAuthoritative() const;
    const RuntimeMultiplayerMatchSetup& setup() const;

    // Synchronizes the complete authoritative SessionSystem roster in one
    // transaction. Combatants retain their current Human slot when possible;
    // newly joined/promoted combatants take the lowest Closed slot.
    // Spectators and departed members release their Human slot to Closed.
    RuntimeMultiplayerLobbyEditResult syncMembers(
        std::span<const RuntimeMultiplayerRoomMember> authoritativeMembers
    );

    // Reopens only the original Human seats. Computers, Closed holes, and
    // every seat's options stay authored by the save. Existing assignments
    // survive joins/leaves; stable identity/name matches precede free seats.
    static bool createResumeHost(const RuntimeMultiplayerMatchSetup& saved,
        const RuntimeMultiplayerRoomMember& host, RuntimeMultiplayerLobbyModel& model,
        std::string& errorMessage);
    RuntimeMultiplayerLobbyEditResult syncResumeMembers(
        std::span<const RuntimeMultiplayerRoomMember> members,
        const RuntimeMultiplayerMatchSetup& saved);

    RuntimeMultiplayerLobbyEditResult setMap(
        const RuntimeMultiplayerMatchMap& map
    );
    RuntimeMultiplayerLobbyEditResult setRules(
        const RuntimeMultiplayerMatchRules& rules
    );
    RuntimeMultiplayerLobbyEditResult setMapAndRules(
        const RuntimeMultiplayerMatchMap& map,
        const RuntimeMultiplayerMatchRules& rules
    );

    // A Human controller needs an existing Combatant peer. Moving a Human
    // between slots should use replaceSlots so both sides change atomically.
    RuntimeMultiplayerLobbyEditResult setSlotController(
        std::size_t slotIndex,
        RuntimeMultiplayerMatchSlotController controller,
        std::string_view humanPeerId = {}
    );
    RuntimeMultiplayerLobbyEditResult setSlotOptions(
        std::size_t slotIndex,
        std::string countryId,
        std::string colorId,
        std::string teamId,
        std::string difficultyId,
        std::uint8_t startPosition
    );
    RuntimeMultiplayerLobbyEditResult setSlot(
        std::size_t slotIndex,
        const RuntimeMultiplayerMatchSlot& slot
    );
    RuntimeMultiplayerLobbyEditResult replaceSlots(
        std::span<const RuntimeMultiplayerMatchSlot> slots
    );

    // Clients accept only a canonical host draft image. Exact replay is
    // idempotent; rollback and same-revision conflicts are rejected.
    RuntimeMultiplayerLobbyEditResult replaceCanonicalSetup(
        const RuntimeMultiplayerMatchSetup& canonicalSetup
    );

    // Ready peers are supplied by SessionSystem and must name every current
    // member exactly once, including spectators. The setup itself never stores
    // ready state.
    bool canStart(
        std::span<const std::string> readyPeerIds,
        std::string& errorMessage
    ) const;

private:
    RuntimeMultiplayerLobbyEditResult commitHostCandidate(
        RuntimeMultiplayerMatchSetup candidate
    );

    RuntimeMultiplayerLobbyAuthority authority_ =
        RuntimeMultiplayerLobbyAuthority::Uninitialized;
    RuntimeMultiplayerMatchSetup setup_;
};
