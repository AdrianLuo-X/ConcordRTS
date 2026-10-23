#pragma once

#include "simulation/abilities/unit_special/RuntimeSpecialMechanismEvents.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

enum class TimedBombKind {
    C4,
    Ivan
};

struct TimedBombState {
    int targetObjectId = -1;
    int sourceObjectId = -1;
    std::string owner;
    TimedBombKind kind = TimedBombKind::Ivan;
    int attachedTick = 0;
    int detonateTick = 0;
    int damage = 0;
    std::string warheadId;
    // Stock C4 uses C4Warhead as the engine's absolute-damage marker and
    // destroys a building regardless of its current hit points.
    bool forceDestruction = false;
};

struct TimedBombRules {
    double c4DelayMinutes = 0.0;
    std::string c4Warhead;
    int ivanTimedDelayFrames = 0;
    int ivanDamage = 0;
    std::string ivanWarhead;
};

// Immutable facts for one object; the module never receives a world or shell.
struct TimedBombObjectView {
    bool present = false;
    bool alive = false;
    int health = 0;
    std::string owner;
    SpecialCell cell;
};
enum class TimedBombEligibility { C4Source, IvanSource, C4Target, IvanTarget };
using TimedBombObjectQuery = std::function<TimedBombObjectView(int)>;
using TimedBombEligibilityQuery = std::function<bool(int, TimedBombEligibility)>;
using TimedBombDisarmQuery = std::function<bool(const std::string&)>;

class RuntimeTimedBombState {
public:
    void reset();
    void swap(RuntimeTimedBombState& other) noexcept;
    const std::map<int, TimedBombState>& records() const;

    bool tryPlantC4(const TimedBombObjectQuery& objects,
        const TimedBombEligibilityQuery& eligibility, const TimedBombRules& rules,
        int sourceObjectId, int targetObjectId, int tick, int fixedTickHz,
        const SpecialEventSink& emit, std::string& reason);
    bool tryAttachIvanBomb(const TimedBombObjectQuery& objects,
        const TimedBombEligibilityQuery& eligibility, const TimedBombRules& rules,
        int sourceObjectId, int targetObjectId, int tick, int fixedTickHz,
        const SpecialEventSink& emit, std::string& reason);
    bool tryDisarmTimedBomb(const TimedBombObjectQuery& objects,
        const TimedBombDisarmQuery& disarms, int sourceObjectId, int targetObjectId,
        const std::string& warheadId, int tick, const SpecialEventSink& emit,
        std::string& reason);
    std::vector<SpecialMechanismAction> tickTimedBombs(
        int tick, const SpecialEventSink& emit);
    void onObjectRemoved(const TimedBombObjectQuery& objects, int objectId,
        int tick, const SpecialEventSink& emit, const std::string& reason,
        std::vector<SpecialMechanismAction>* actions);
    void onOwnerChanged(int objectId, const std::string& newOwner);
    void reconcile(const TimedBombObjectQuery& objects, int tick,
        const SpecialEventSink& emit, std::vector<SpecialMechanismAction>* actions);

private:
    // Only the existing aggregate codec may populate this map while its
    // whole-SpecialMechanisms load transaction is still uncommitted.
    friend class RuntimeSpecialMechanismState;
    std::map<int, TimedBombState> byTarget_;
};
