#pragma once

#include "GameWorld.h"
#include "RulesDatabase.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <limits>
#include <optional>
#include <string>
#include <vector>

class IniFile;

struct RuntimeRepairDepotCell {
    int x = -1;
    int y = -1;
    bool onBridge = false;

    bool operator==(const RuntimeRepairDepotCell&) const = default;
};

enum class RuntimeRepairDepotRelationPhase {
    Queued,
    Reserved,
    Docked
};

enum class RuntimeRepairDepotDomain {
    Ground,
    Naval
};

enum class RuntimeRepairDepotAnimationPhase {
    Extending,
    Repairing,
    Retracting
};

struct RuntimeRepairDepotAnimationState {
    int hostId = -1;
    RuntimeRepairDepotAnimationPhase phase =
        RuntimeRepairDepotAnimationPhase::Extending;
    int ageTicks = 0;

    bool operator==(const RuntimeRepairDepotAnimationState&) const = default;
};

enum class RuntimeRepairDepotVeto {
    None,
    InvalidContext,
    HostMissing,
    HostUnavailable,
    HostNotRepairDepot,
    UnitMissing,
    UnitUnavailable,
    UnitNotGroundVehicle,
    UnitNotRepairable,
    UnitAlreadyManaged,
    UnitAlreadyDocked,
    UnitTransported,
    UnitGarrisoned,
    NotAllied,
    UnitAlreadyFull,
    CapacityFull,
    QueueCellUnavailable,
    DockCellUnavailable,
    PathUnavailable,
    ReservationMissing,
    ReservationMismatch,
    UnitNotAtDock,
    DomainUnavailable,
    DomainMismatch,
    InvalidTransition,
    RefundUnavailable
};

enum class RuntimeRepairDepotActionKind {
    MoveToQueue,
    MoveToDock,
    Attach,
    Repair,
    UndockToRally,
    Release,
    DestroyDockedUnit,
    SellDockedUnit,
    TransferDockedUnitOwner
};

enum class RuntimeRepairDepotHostTransitionKind {
    Destroyed,
    Sold,
    Captured
};

// Actions are deliberately value-only.  RuntimeShell must apply every returned
// batch atomically and in order (without saving/reconciling in between), after
// it has left any range-loop that health, credits, docking, movement or audio
// callbacks could invalidate.
struct RuntimeRepairDepotAction {
    RuntimeRepairDepotActionKind kind = RuntimeRepairDepotActionKind::Release;
    int hostId = -1;
    int unitId = -1;
    int dockIndex = -1;
    int queueIndex = -1;
    RuntimeRepairDepotRelationPhase relationPhase =
        RuntimeRepairDepotRelationPhase::Reserved;
    bool hasTargetCell = false;
    RuntimeRepairDepotCell targetCell;
    // Canonical economy account id for Repair actions.
    std::string owner;
    int healthBefore = 0;
    int healthAfter = 0;
    int healedHitPoints = 0;
    int creditCost = 0;
    bool firstRepairPulse = false;
    std::string reason;
    RuntimeRepairDepotDomain domain = RuntimeRepairDepotDomain::Ground;
    std::string expectedOwner;
    std::string newOwner;
    int refundValue = 0;
};

struct RuntimeRepairDepotCommandResult {
    bool accepted = false;
    RuntimeRepairDepotVeto veto = RuntimeRepairDepotVeto::None;
    int hostId = -1;
    int unitId = -1;
    int dockIndex = -1;
    int queueIndex = -1;
    std::string reason;
    std::vector<RuntimeRepairDepotAction> actions;
};

struct RuntimeRepairDepotRelation {
    int hostId = -1;
    int unitId = -1;
    int dockIndex = -1;
    int queueIndex = -1;
    RuntimeRepairDepotRelationPhase phase =
        RuntimeRepairDepotRelationPhase::Reserved;
    RuntimeRepairDepotCell dockCell;
    RuntimeRepairDepotCell queueCell;
    std::string hostOwner;
    std::string unitOwner;
    std::string hostType;
    std::string unitType;
    int reservedTick = 0;
    int attachedTick = -1;
    bool hasRepaired = false;
    RuntimeRepairDepotDomain domain = RuntimeRepairDepotDomain::Ground;
};

struct RuntimeRepairDepotContext {
    const RulesDatabase* rules = nullptr;
    const std::vector<WorldObjectInstance>* objects = nullptr;

    // Strict save/load validation may reconcile a snapshot without advancing
    // queued work into newly available docks. False forces a zero promotion
    // budget. The limit is shared by every reconcile pass inside one tick.
    bool allowQueuePromotion = true;
    std::size_t maximumQueuePromotions =
        std::numeric_limits<std::size_t>::max();

    std::function<bool(const std::string&, const std::string&)> areAllied;
    std::function<int(const std::string&)> creditBalance;
    std::function<std::optional<RuntimeRepairDepotCell>(
        int hostId,
        int dockIndex,
        int unitId
    )> resolveDockCell;
    std::function<std::optional<RuntimeRepairDepotCell>(
        int hostId,
        int queueIndex,
        int unitId
    )> resolveQueueCell;
    std::function<std::optional<RuntimeRepairDepotCell>(
        int hostId,
        int dockIndex,
        int unitId
    )> resolveRallyCell;
    std::function<bool(int unitId, const RuntimeRepairDepotCell&)> canPathTo;
    // Optional authoritative command/task check. For Queued relations the
    // phase-local slot is queueIndex; for Reserved relations it is dockIndex.
    // RuntimeShell should return false after either repair move is cancelled,
    // fails or is superseded.
    std::function<bool(int hostId, int phaseSlotIndex, int unitId)>
        isReservationActive;
    // WorldObjectInstance has no reverse transport-parent id.  The host owns
    // that manifest, so RuntimeShell supplies this authoritative lookup.
    std::function<bool(int unitId)> isTransported;
    // Maps owner spelling/aliases to the exact economy account id consumed by
    // creditBalance and returned in Repair actions.
    std::function<std::string(const std::string&)> canonicalOwnerId;
    // Accounts for aircraft pads and other docking systems not owned here.
    std::function<bool(int hostId, int dockIndex)> isDockOccupied;
    // Domain is authored by the host integration. The core never infers it
    // from locomotor, Naval, WaterBound or any other movement capability.
    std::function<std::optional<RuntimeRepairDepotDomain>(int hostId)>
        resolveHostDomain;
    std::function<std::optional<RuntimeRepairDepotDomain>(int unitId)>
        resolveUnitDomain;
    // Returns the complete sale refund for a docked ground unit. A missing
    // value vetoes the entire host transition before state is changed.
    std::function<std::optional<int>(int unitId)> refundValue;
    // Native SpecialAnim / SpecialAnimThree durations in simulation ticks.
    // Repairing loops until service stops and does not use this duration.
    std::function<int(int hostId, RuntimeRepairDepotAnimationPhase phase)>
        animationDurationTicks;
};

struct RuntimeRepairDepotHostTransitionRequest {
    int hostId = -1;
    RuntimeRepairDepotHostTransitionKind kind =
        RuntimeRepairDepotHostTransitionKind::Destroyed;
    bool temporalRemoval = false;
    std::string expectedOwner;
    std::string newOwner;
};

struct RuntimeRepairDepotHostTransitionResult {
    bool accepted = false;
    RuntimeRepairDepotVeto veto = RuntimeRepairDepotVeto::None;
    int hostId = -1;
    std::string reason;
    std::vector<RuntimeRepairDepotAction> actions;
};

struct RuntimeRepairDepotReconcileReport {
    std::size_t retainedRelations = 0;
    std::size_t releasedRelations = 0;
    std::size_t removedHostTimers = 0;
    std::size_t removedHostAnimations = 0;
    std::vector<RuntimeRepairDepotAction> actions;
};

class RuntimeRepairDepotSystemState {
public:
    explicit RuntimeRepairDepotSystemState(int fixedTickHz = 30);

    void reset(int fixedTickHz = 30);
    int fixedTickHz() const;

    RuntimeRepairDepotCommandResult reserveDock(
        const RuntimeRepairDepotContext& context,
        int hostId,
        int unitId,
        int currentTick
    );
    RuntimeRepairDepotCommandResult attachAtDock(
        const RuntimeRepairDepotContext& context,
        int hostId,
        int unitId,
        const RuntimeRepairDepotCell& actualCell,
        int currentTick
    );
    std::vector<RuntimeRepairDepotAction> releaseUnit(
        const RuntimeRepairDepotContext& context,
        int unitId,
        const std::string& reason
    );
    RuntimeRepairDepotHostTransitionResult transitionHost(
        const RuntimeRepairDepotContext& context,
        const RuntimeRepairDepotHostTransitionRequest& request
    );
    bool claimsDock(int hostId, int dockIndex) const;

    std::vector<RuntimeRepairDepotAction> tick(
        const RuntimeRepairDepotContext& context,
        int currentTick
    );
    RuntimeRepairDepotReconcileReport reconcile(
        const RuntimeRepairDepotContext& context
    );

    void save(std::ostream& output) const;
    bool load(
        const IniFile& ini,
        const RuntimeRepairDepotContext& context,
        std::string* errorMessage = nullptr,
        RuntimeRepairDepotReconcileReport* reconcileReport = nullptr
    );
    std::uint64_t stateHash() const;

    const std::vector<RuntimeRepairDepotRelation>& relations() const;
    const std::vector<RuntimeRepairDepotAnimationState>& animations() const;
    int remainingServiceTicks(int hostId) const;
    int lastAdvancedTick() const;

private:
    struct HostTimer {
        int hostId = -1;
        int remainingTicks = 0;
    };

    int fixedTickHz_ = 30;
    int lastAdvancedTick_ = -1;
    std::vector<RuntimeRepairDepotRelation> relations_;
    std::vector<HostTimer> hostTimers_;
    std::vector<RuntimeRepairDepotAnimationState> animations_;

    void startAnimation(int hostId);
    void reconcileAnimations(const RuntimeRepairDepotContext& context);
    void advanceAnimations(
        const RuntimeRepairDepotContext& context,
        int elapsedTicks
    );
    void updateAnimationRequests(
        const RuntimeRepairDepotContext& context,
        const std::vector<RuntimeRepairDepotAction>& actions
    );

    RuntimeRepairDepotReconcileReport reconcileWithPromotionBudget(
        const RuntimeRepairDepotContext& context,
        std::size_t& remainingQueuePromotions
    );
    void sortState();
};

const char* runtimeRepairDepotVetoName(RuntimeRepairDepotVeto veto);
const char* runtimeRepairDepotActionName(RuntimeRepairDepotActionKind kind);
const char* runtimeRepairDepotDomainName(RuntimeRepairDepotDomain domain);
