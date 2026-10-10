#pragma once

#include "world/model/GameWorld.h"
#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct RuntimeGarrisonConfig {
    // Stock rulesmd.ini defaults.  These remain explicit inputs until the
    // owning runtime parses the corresponding [AudioVisual]/[CombatDamage]
    // keys.
    double conditionRedFraction = 0.25;
    double occupyDamageMultiplier = 1.2;
    double occupyRofMultiplier = 1.2;
    double occupyWeaponRange = 5.0;
    int fixedTickHz = 30;
};

enum class RuntimeGarrisonEntryStatus {
    Entered,
    MissingOccupant,
    MissingHost,
    SameObject,
    OccupantNotAlive,
    OccupantNotInfantry,
    MissingOccupantType,
    OccupantNotOccupier,
    HostNotAlive,
    HostNotStructure,
    MissingHostType,
    HostNotOccupiable,
    HostHasNoCapacity,
    DifferentOwner,
    HostAtConditionRed,
    OccupantInTransport,
    OccupantAlreadyGarrisoned,
    HostAtCapacity,
    Vetoed
};

struct RuntimeGarrisonEntryResult {
    RuntimeGarrisonEntryStatus status =
        RuntimeGarrisonEntryStatus::MissingOccupant;
    std::string reason;

    bool entered() const {
        return status == RuntimeGarrisonEntryStatus::Entered;
    }
};

// Return an explanatory string to veto entry, or an empty string to allow it.
// The runtime can use this hook for mind-control, parasite, temporal, or other
// state owned by modules that deliberately are not dependencies here.
using RuntimeGarrisonEntryVeto = std::function<std::string(
    const WorldObjectInstance& occupant,
    const WorldObjectInstance& host
)>;

struct RuntimeGarrisonExitPlacement {
    int x = -1;
    int y = -1;
    int cell = -1;
    int level = 0;
    bool onBridge = false;
};

enum class RuntimeGarrisonEvacuationReason {
    Manual,
    ConditionRed,
    Forced,
    HostDestroyed
};

using RuntimeGarrisonExitResolver = std::function<
    std::optional<RuntimeGarrisonExitPlacement>(
        const WorldObjectInstance& occupant,
        const WorldObjectInstance& host,
        RuntimeGarrisonEvacuationReason reason,
        std::size_t manifestIndex
    )
>;

enum class RuntimeGarrisonExitStatus {
    Exited,
    MissingOccupant,
    NotGarrisoned,
    MissingHost,
    NoExitPlacement
};

struct RuntimeGarrisonExitResult {
    RuntimeGarrisonExitStatus status =
        RuntimeGarrisonExitStatus::MissingOccupant;
    int occupantId = -1;
    int hostId = -1;

    bool exited() const {
        return status == RuntimeGarrisonExitStatus::Exited;
    }
};

enum class RuntimeGarrisonEvacuationDisposition {
    Exited,
    DestroyedWithHost,
    DestroyedNoExit,
    KeptInside
};

struct RuntimeGarrisonEvacuationAction {
    int occupantId = -1;
    int hostId = -1;
    RuntimeGarrisonEvacuationDisposition disposition =
        RuntimeGarrisonEvacuationDisposition::KeptInside;
};

enum class RuntimeGarrisonSelectiveEvacuationStatus {
    Applied,
    InvalidHost,
    DuplicateOccupant,
    OccupantNotInHost
};

struct RuntimeGarrisonSelectiveEvacuationResult {
    RuntimeGarrisonSelectiveEvacuationStatus status =
        RuntimeGarrisonSelectiveEvacuationStatus::InvalidHost;
    int rejectedOccupantId = -1;
    std::vector<RuntimeGarrisonEvacuationAction> actions;

    bool applied() const {
        return status == RuntimeGarrisonSelectiveEvacuationStatus::Applied;
    }
};

struct RuntimeGarrisonFirePlan {
    int hostObjectId = -1;
    int occupantObjectId = -1;
    int occupantIndex = -1;
    int targetObjectId = -1;
    std::string weaponId;
    std::string projectileId;
    std::string warheadId;
    int baseDamage = 0;
    int damage = 0;
    int baseRof = 0;
    int cooldownTicks = 0;
    int nextEligibleTick = 0;
    double range = 0.0;
    double minimumRange = 0.0;
    int burst = 1;
};

using RuntimeGarrisonFirePredicate = std::function<bool(
    int occupantObjectId,
    const WeaponType& weapon
)>;

enum class RuntimeGarrisonRecoveryKind {
    RecoveredReverseOnly,
    RecoveredLegacyOccupant,
    DroppedCorruptRelation,
    NeedsExitPlacement
};

struct RuntimeGarrisonRecoveryAction {
    RuntimeGarrisonRecoveryKind kind =
        RuntimeGarrisonRecoveryKind::DroppedCorruptRelation;
    int occupantId = -1;
    int hostId = -1;
    std::string reason;
};

struct RuntimeGarrisonReconcileReport {
    std::size_t retainedRelations = 0;
    std::size_t removedRelations = 0;
    std::size_t recoveredRelations = 0;
    std::size_t removedCooldowns = 0;
    std::vector<RuntimeGarrisonRecoveryAction> actions;
};

class RuntimeGarrisonSystemState {
public:
    RuntimeGarrisonSystemState() = default;
    explicit RuntimeGarrisonSystemState(RuntimeGarrisonConfig config);

    void swap(RuntimeGarrisonSystemState& other) noexcept;

    void reset(RuntimeGarrisonConfig config = {});
    const RuntimeGarrisonConfig& config() const;

    RuntimeGarrisonEntryResult tryEnter(
        const RulesDatabase& rules,
        std::vector<WorldObjectInstance>& objects,
        int occupantObjectId,
        int hostObjectId,
        const RuntimeGarrisonEntryVeto& veto = {}
    );

    void syncHostOccupants(
        std::vector<WorldObjectInstance>& objects,
        int hostObjectId
    ) const;

    std::optional<RuntimeGarrisonFirePlan> takeFirePlan(
        const RulesDatabase& rules,
        std::vector<WorldObjectInstance>& objects,
        int hostObjectId,
        int targetObjectId,
        int currentTick,
        const RuntimeGarrisonFirePredicate& canFire = {}
    );

    RuntimeGarrisonExitResult exitOccupant(
        std::vector<WorldObjectInstance>& objects,
        int occupantObjectId,
        const std::optional<RuntimeGarrisonExitPlacement>& placement
    );

    std::vector<RuntimeGarrisonEvacuationAction> forceEvacuate(
        std::vector<WorldObjectInstance>& objects,
        int hostObjectId,
        RuntimeGarrisonEvacuationReason reason,
        const RuntimeGarrisonExitResolver& resolver,
        bool destroyWhenBlocked = true
    );

    // Validation is all-or-nothing and precedes resolver invocation.  A valid
    // selection is always processed in the host manifest's original order,
    // independent of the caller-provided id order.
    RuntimeGarrisonSelectiveEvacuationResult forceEvacuateOccupants(
        std::vector<WorldObjectInstance>& objects,
        int hostObjectId,
        const std::vector<int>& occupantObjectIds,
        RuntimeGarrisonEvacuationReason reason,
        const RuntimeGarrisonExitResolver& resolver,
        bool destroyWhenBlocked = true
    );

    std::vector<RuntimeGarrisonEvacuationAction> evacuateIfConditionRed(
        const RulesDatabase& rules,
        std::vector<WorldObjectInstance>& objects,
        int hostObjectId,
        const RuntimeGarrisonExitResolver& resolver,
        bool destroyWhenBlocked = true
    );

    std::vector<RuntimeGarrisonEvacuationAction> handleHostDestruction(
        std::vector<WorldObjectInstance>& objects,
        int hostObjectId,
        bool immediate,
        const RuntimeGarrisonExitResolver& resolver = {}
    );

    RuntimeGarrisonReconcileReport reconcile(
        const RulesDatabase& rules,
        std::vector<WorldObjectInstance>& objects
    );

    void save(
        const std::vector<WorldObjectInstance>& objects,
        std::ostream& output
    ) const;
    bool load(
        const IniFile& ini,
        const RulesDatabase& rules,
        std::vector<WorldObjectInstance>& objects,
        std::string* errorMessage = nullptr,
        RuntimeGarrisonReconcileReport* reconcileReport = nullptr
    );
    std::uint64_t stateHash(
        const std::vector<WorldObjectInstance>& objects
    ) const;

    int nextEligibleFireTick(int hostObjectId) const;

private:
    std::vector<RuntimeGarrisonEvacuationAction>
    evacuateValidatedOccupants(
        std::vector<WorldObjectInstance>& objects,
        int hostObjectId,
        const std::vector<int>& occupantObjectIds,
        RuntimeGarrisonEvacuationReason reason,
        const RuntimeGarrisonExitResolver& resolver,
        bool destroyWhenBlocked
    );

    RuntimeGarrisonConfig config_;
    // An occupied structure has one weapon cadence.  The selected actual
    // occupant changes round-robin, but the cooldown belongs to the host.
    std::map<int, int> nextFireTickByHostId_;

    int convertRuleFrames(double authoredFrames) const;
};

void swap(
    RuntimeGarrisonSystemState& left,
    RuntimeGarrisonSystemState& right
) noexcept;
