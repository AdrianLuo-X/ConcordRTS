#pragma once

#include "CooperativeScenarioDatabase.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class IniFile;

inline constexpr std::size_t runtimeCooperativeSlotCount = 4;

enum class RuntimeCooperativeSessionStatus : std::uint8_t {
    Inactive,
    Running,
    PlayerCoalitionVictory,
    EnemyCoalitionVictory,
    Draw
};

// Cooperative campaign maps rely on their authored trigger/team evaluator.
// Several stock maps do not author a complete playable force for every A-D
// participant, so the host must also apply the fixed-waypoint start plan.
// This value-only core never creates world objects itself.
struct RuntimeCooperativeBootstrapPolicy {
    bool useAuthoredTriggers = true;
    bool requiresCooperativeStartBootstrap = true;
    bool coreCreatesWorldObjects = false;
};

struct RuntimeCooperativeStartBootstrapInstruction {
    int slotIndex = -1;
    std::string owner;
    std::string country;
    int waypointId = -1;
    CooperativeScenarioWaypoint start;
    bool includeMcv = true;
    bool includeBasicStartingUnits = true;
};

struct RuntimeCooperativeBootstrapPlan {
    RuntimeCooperativeBootstrapPolicy policy;
    std::array<RuntimeCooperativeStartBootstrapInstruction,
               runtimeCooperativeSlotCount> starts;
};

struct RuntimeCooperativeSessionSlot {
    int slotIndex = -1;
    char label = '?';
    int authoredOwnerId = -1;
    std::string authoredHouse;
    CooperativeSlotRole role = CooperativeSlotRole::AiEnemy;
    int coalitionId = -1;
    int teamId = -1;
    int waypointId = -1;
    CooperativeScenarioWaypoint start;

    // The host may bind A-D to its canonical house/account names. Empty build
    // options use the exact authored <Player @ X> aliases.
    std::string owner;
    // Every member of a coalition receives the complete corresponding list.
    // country is one selected value from that shared constraint vector.
    std::vector<std::string> allowedCountries;
    std::string country;
};

struct RuntimeCooperativeSessionBuildOptions {
    std::array<std::string, runtimeCooperativeSlotCount> owners;
    std::array<std::string, runtimeCooperativeSlotCount> countries;
};

struct RuntimeCooperativeSessionBuildResult {
    bool accepted = false;
    std::string reason;
};

// Counts must include every live techno owned by the corresponding authored
// participant: infantry, vehicles, aircraft and buildings. ShortGame and
// building-only filters do not belong in this snapshot.
struct RuntimeCooperativeLiveTechnoSnapshot {
    int tick = -1;
    std::array<int, runtimeCooperativeSlotCount> liveTechnos{};
};

struct RuntimeCooperativeOutcomeAdvanceResult {
    bool accepted = false;
    bool changed = false;
    RuntimeCooperativeSessionStatus before =
        RuntimeCooperativeSessionStatus::Inactive;
    RuntimeCooperativeSessionStatus after =
        RuntimeCooperativeSessionStatus::Inactive;
    int playerCoalitionLiveTechnos = 0;
    int enemyCoalitionLiveTechnos = 0;
    std::string reason;
};

struct RuntimeCooperativeSessionReconcileReport {
    bool contextValid = false;
    bool retained = false;
    bool resetForMapChange = false;
    std::string reason;
};

class RuntimeCooperativeSessionState {
public:
    RuntimeCooperativeSessionState() = default;

    void reset();

    RuntimeCooperativeSessionBuildResult begin(
        const CooperativeScenarioDefinition& scenario,
        const CooperativeScenarioMapValidation& mapValidation,
        RuntimeCooperativeSessionBuildOptions options = {}
    );

    RuntimeCooperativeOutcomeAdvanceResult advanceOutcome(
        const RuntimeCooperativeLiveTechnoSnapshot& snapshot
    );

    RuntimeCooperativeSessionReconcileReport reconcile(
        const CooperativeScenarioDefinition& scenario,
        const CooperativeScenarioMapValidation& mapValidation
    );

    // Trigger owner aliases are intentionally strict. No trimming, case
    // folding, prefix matching or lookup by the host's runtime owner occurs.
    std::optional<int> resolveAuthoredSlotAlias(
        std::string_view alias
    ) const;

    bool slotsAreAllied(int leftSlotIndex, int rightSlotIndex) const;
    RuntimeCooperativeBootstrapPlan bootstrapPlan() const;

    void save(std::ostream& output) const;
    bool load(
        const IniFile& ini,
        const CooperativeScenarioDefinition& scenario,
        const CooperativeScenarioMapValidation& mapValidation,
        std::string* errorMessage = nullptr
    );
    std::uint64_t stateHash() const;

    bool active() const;
    RuntimeCooperativeSessionStatus status() const;
    const std::string& scenarioId() const;
    const std::string& mapId() const;
    std::uint64_t scenarioFingerprint() const;
    std::uint64_t mapFingerprint() const;
    const std::array<RuntimeCooperativeSessionSlot,
                     runtimeCooperativeSlotCount>& slots() const;
    bool hasOutcomeSnapshot() const;
    const RuntimeCooperativeLiveTechnoSnapshot& lastOutcomeSnapshot() const;

private:
    RuntimeCooperativeSessionStatus status_ =
        RuntimeCooperativeSessionStatus::Inactive;
    std::string scenarioId_;
    std::string mapId_;
    std::string mapIdentity_;
    std::uint64_t scenarioFingerprint_ = 0;
    std::uint64_t mapFingerprint_ = 0;
    std::array<RuntimeCooperativeSessionSlot,
               runtimeCooperativeSlotCount> slots_{};
    bool hasOutcomeSnapshot_ = false;
    RuntimeCooperativeLiveTechnoSnapshot lastOutcomeSnapshot_;
};

RuntimeCooperativeBootstrapPolicy runtimeCooperativeBootstrapPolicy();

const char* runtimeCooperativeSessionStatusName(
    RuntimeCooperativeSessionStatus status
);

std::optional<int> runtimeCooperativeScenarioSlotIndexFromAlias(
    const CooperativeScenarioDefinition& scenario,
    std::string_view alias
);
