#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct WorldObjectInstance;

namespace concord::runtime::session_detail {

struct RuntimeProductionHostState;

struct RuntimeSkirmishOutcomeHostQueries {
    RuntimeProductionHostState& state;

    std::size_t playerSlotCount() const;
    std::string ownerForSlot(std::size_t slot) const;
    bool ownerDefeated(const std::string& owner) const;
    bool ownersAreAllied(const std::string& first, const std::string& second) const;
    bool ownerIsNeutral(const std::string& owner) const;
    const std::vector<WorldObjectInstance>& worldObjects() const;
    void observeDefeatNotices();
    void finishSimulation();
};

} // namespace concord::runtime::session_detail
