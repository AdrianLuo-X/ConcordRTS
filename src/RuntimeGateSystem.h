#pragma once

#include "GameWorld.h"
#include "RulesDatabase.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

class IniFile;

enum class RuntimeGatePhase {
    Closed,
    Opening,
    Open,
    Closing
};

struct RuntimeGateConfig {
    int fixedTickHz = 30;
    int openingTicks = 15;
    int closingTicks = 15;
};

struct RuntimeGatePassageRequest {
    int gateId = -1;
    int moverId = -1;
};

// RuntimeShell owns proximity/path intent.  It supplies one value-only request
// while a mover approaches the gate; this module remains independent of the
// pathfinder and can therefore keep transition/save semantics deterministic.
struct RuntimeGateContext {
    const RulesDatabase* rules = nullptr;
    const std::vector<WorldObjectInstance>* objects = nullptr;
    const std::vector<RuntimeGatePassageRequest>* passageRequests = nullptr;

    std::function<bool(const std::string&, const std::string&)> areAllied;
    // Optional authoritative override for locomotor/path-layer knowledge.
    // Basic infantry/vehicle classification is used when this is absent.
    std::function<bool(const WorldObjectInstance&)> isGroundMover;
    // True while any live object occupies the gate footprint or doorway.
    std::function<bool(int gateId)> doorwayOccupied;
};

struct RuntimeGateInstanceState {
    int gateId = -1;
    std::string owner;
    std::string type;
    RuntimeGatePhase phase = RuntimeGatePhase::Closed;
    // Transition spans retain their full authored duration.  Reversing a
    // partially closed gate uses a virtual opening start (which may be
    // negative early in a match) so render progress remains continuous.
    int phaseStartedTick = -1;
    int phaseCompletesTick = -1;
    int lastActivityTick = -1;
};

enum class RuntimeGateActionKind {
    BeginOpening,
    FullyOpen,
    BeginClosing,
    FullyClosed
};

// Actions are deliberately value-only.  The host applies animation,
// passability-cache and audio changes after leaving object iteration.
struct RuntimeGateAction {
    RuntimeGateActionKind kind = RuntimeGateActionKind::BeginOpening;
    int gateId = -1;
    std::string owner;
    std::string type;
    RuntimeGatePhase fromPhase = RuntimeGatePhase::Closed;
    RuntimeGatePhase toPhase = RuntimeGatePhase::Closed;
    int eventTick = -1;
    // BeginOpening carries GateUp and BeginClosing carries GateDown.  soundId
    // is the resolved [AudioVisual] value and may intentionally be empty.
    std::string soundKey;
    std::string soundId;
    std::string reason;
};

struct RuntimeGateReconcileReport {
    bool contextValid = false;
    std::size_t retainedGates = 0;
    std::size_t addedGates = 0;
    std::size_t removedGates = 0;
    std::size_t resetGates = 0;
    std::size_t refreshedOwners = 0;
    std::size_t skippedByLimit = 0;
    std::vector<int> addedGateIds;
    std::vector<int> removedGateIds;
    std::vector<int> resetGateIds;
};

class RuntimeGateSystemState {
public:
    RuntimeGateSystemState();
    explicit RuntimeGateSystemState(RuntimeGateConfig config);

    void reset(RuntimeGateConfig config = {});
    const RuntimeGateConfig& config() const;

    std::vector<RuntimeGateAction> tick(
        const RuntimeGateContext& context,
        int currentTick
    );
    RuntimeGateReconcileReport reconcile(
        const RuntimeGateContext& context
    );

    // A closed, opening or closing gate remains a hard blocker.  Once Fully
    // Open, every valid ground mover may pass, regardless of alliance.
    bool allowsPassage(
        const RuntimeGateContext& context,
        int gateId,
        int moverId
    ) const;

    RuntimeGatePhase phaseOf(int gateId) const;
    const RuntimeGateInstanceState* findGate(int gateId) const;
    const std::vector<RuntimeGateInstanceState>& gates() const;
    int lastAdvancedTick() const;
    int closeDelayTicks(
        const RuntimeGateContext& context,
        int gateId
    ) const;

    void save(std::ostream& output) const;
    bool load(
        const IniFile& ini,
        const RuntimeGateContext& context,
        std::string* errorMessage = nullptr,
        RuntimeGateReconcileReport* reconcileReport = nullptr
    );
    std::uint64_t stateHash() const;

private:
    RuntimeGateConfig config_;
    int lastAdvancedTick_ = -1;
    std::vector<RuntimeGateInstanceState> gates_;

    void sortState();
};

const char* runtimeGatePhaseName(RuntimeGatePhase phase);
const char* runtimeGateActionName(RuntimeGateActionKind kind);
