#pragma once
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include "core/io/RuntimeSaveTextFields.h"
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace runtime_skirmish_outcome {
// These references borrow the existing session/economy owners; no copy or new state.
struct ReadInput {
    const bool& campaignMode;
    const std::string& gameMode;
    const std::unordered_map<std::string, std::string>& countryByOwner;
    const bool& startApplied;
    const int& tick;
    const std::string& localOwner;
    const bool& shortGame;
};
struct WriteOutput {
    bool& finished;
    bool& victory;
    std::string& result;
    std::string& skirmishStatus;
    std::string& economyStatus;
    std::string& interactionStatus;
};

// Queries exposes only existing owner/alliance facts, original ordered object
// access, notices at the old point, and the old end-of-match cleanup action.
template<class Queries>
bool advanceSinglePlayerSkirmishOutcome(
    const ReadInput& input, WriteOutput output, Queries& queries) {
    using runtime_save_text::trimAsciiLocal;
    using runtime_save_text::toLowerAsciiLocal;
    using runtime_authority_hash::toUpperAsciiLocal;
    if (input.campaignMode
        || toUpperAsciiLocal(trimAsciiLocal(input.gameMode))
            == "CAMPAIGNMISSION"
        || output.finished
        || input.countryByOwner.empty()
        || !input.startApplied
        || input.tick < 5
        || (input.tick % 10) != 0) {
        return false;
    }

    queries.observeDefeatNotices();
    if (queries.ownerDefeated(input.localOwner)) {
        output.finished = true;
        output.victory = false;
        output.result = "DEFEAT";
    } else {
        bool hostileSurvivor = false;
        for (const auto& [ownerKey, country] : input.countryByOwner) {
            (void)country;
            std::string owner;
            for (std::size_t slotIndex = 0;
                slotIndex < queries.playerSlotCount();
                ++slotIndex) {
                const std::string candidate =
                    queries.ownerForSlot(slotIndex);
                if (toLowerAsciiLocal(candidate) == ownerKey) {
                    owner = candidate;
                    break;
                }
            }
            if (owner.empty()
                || queries.ownersAreAllied(input.localOwner, owner)
                || queries.ownerDefeated(owner)) {
                continue;
            }
            hostileSurvivor = true;
            break;
        }
        if (!hostileSurvivor
            && toUpperAsciiLocal(trimAsciiLocal(input.gameMode))
                == "COOPERATIVE") {
            std::unordered_set<std::string> authoredOwners;
            for (const auto& object : queries.worldObjects()) {
                if (!object.alive
                    || queries.ownerIsNeutral(object.owner)
                    || object.owner == input.localOwner
                    || input.countryByOwner.find(
                        toLowerAsciiLocal(trimAsciiLocal(object.owner))
                    ) != input.countryByOwner.end()) {
                    continue;
                }
                authoredOwners.insert(object.owner);
            }
            for (const std::string& owner : authoredOwners) {
                if (!queries.ownersAreAllied(input.localOwner, owner)
                    && !queries.ownerDefeated(owner)) {
                    hostileSurvivor = true;
                    break;
                }
            }
        }
        if (hostileSurvivor) {
            return false;
        }
        output.finished = true;
        output.victory = true;
        output.result = "VICTORY";
    }

    output.skirmishStatus =
        output.result
        + std::string(input.shortGame ? " (short game)" : "");
    output.economyStatus = output.skirmishStatus;
    output.interactionStatus = output.skirmishStatus;
    queries.finishSimulation();
    return true;
}

} // namespace runtime_skirmish_outcome
