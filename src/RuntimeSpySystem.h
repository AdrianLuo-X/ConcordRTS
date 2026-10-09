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

enum class RuntimeSpySide : std::uint8_t {
    Allied = 0,
    Soviet = 1,
    Third = 2
};

enum class RuntimeSpyVeteranProduction : std::uint8_t {
    Infantry = 0,
    Vehicles = 1,
    Aircraft = 2
};

enum class RuntimeSpyDisguiseIdentity : std::uint8_t {
    Infantry = 0,
    Vehicle = 1,
    Terrain = 2
};

enum class RuntimeSpyVeto : std::uint8_t {
    None = 0,
    InvalidContext,
    AgentMissing,
    AgentUnavailable,
    AgentNotInfantry,
    AgentCannotInfiltrate,
    TargetMissing,
    TargetUnavailable,
    TargetNotBuilding,
    TargetNotSpyable,
    FriendlyTarget,
    SourceCannotDisguise,
    TargetCannotSupplyDisguise,
    DisguiseClassMismatch,
    WarheadMissing,
    WarheadDoesNotMakeDisguise,
    DefaultDisguiseUnavailable
};

enum class RuntimeSpyActionKind : std::uint8_t {
    ConsumeAgent = 0,
    ResetRadarShroud,
    SetPowerBlackout,
    ClearPowerBlackout,
    ResetSuperWeapon,
    TransferCredits,
    GrantVeteranProduction,
    DisguiseChanged,
    RevealDisguiseToOwner,
    ConcealDisguiseFromOwner,
    GrantStolenTechnology
};

struct RuntimeSpyAction {
    RuntimeSpyActionKind kind = RuntimeSpyActionKind::ConsumeAgent;
    int tick = 0;
    int sourceObjectId = -1;
    int targetObjectId = -1;
    int detectorObjectId = -1;
    std::string sourceOwner;
    std::string targetOwner;
    int amount = 0;
    int durationTicks = 0;
    int untilTick = 0;
    RuntimeSpyVeteranProduction veteranProduction =
        RuntimeSpyVeteranProduction::Infantry;
    RuntimeSpySide stolenTechnology = RuntimeSpySide::Allied;
    RuntimeSpyDisguiseIdentity disguiseIdentity =
        RuntimeSpyDisguiseIdentity::Infantry;
    std::string value;
    bool active = false;
    bool changed = false;
    std::string reason;
};

enum class RuntimeSpyDiagnosticKind : std::uint8_t {
    AgentTraitMissing = 0,
    UnsupportedFactoryType,
    NoKnownInfiltrationEffect,
    InvalidDefaultDisguise,
    InvalidMirageDisguise,
    InvalidSavedState,
    ReconciledState
};

struct RuntimeSpyDiagnostic {
    RuntimeSpyDiagnosticKind kind =
        RuntimeSpyDiagnosticKind::NoKnownInfiltrationEffect;
    int objectId = -1;
    std::string value;
    std::string message;
};

struct RuntimeSpyContext {
    const RulesDatabase* rules = nullptr;
    const std::vector<WorldObjectInstance>* objects = nullptr;

    std::function<bool(const std::string&, const std::string&)> areAllied;
    std::function<std::string(const std::string&)> canonicalOwnerId;
    std::function<bool(const std::string&)> isKnownOwner;
    std::function<double(
        const WorldObjectInstance&,
        const WorldObjectInstance&
    )> cellDistance;

    // A host may keep a value snapshot for an object that is temporarily
    // inactive while contained by another object.  Returning true preserves
    // compatible disguise and detection state during reconciliation.  This
    // callback never makes the object eligible for ordinary live gameplay
    // operations; a missing callback retains the legacy live-only behavior.
    std::function<bool(int objectId)> isObjectRetained;

    // Detection by one combatant is shared with every allied combatant in
    // this list.  If empty, owners of live world objects form the list.
    std::vector<std::string> combatantOwners;
};

struct RuntimeSpyInfiltrationInput {
    int agentObjectId = -1;
    int targetBuildingObjectId = -1;
    int currentTick = 0;
    // Authoritative balance of the building owner before this infiltration.
    int targetOwnerCredits = 0;
};

struct RuntimeSpyInfiltrationResult {
    bool accepted = false;
    RuntimeSpyVeto veto = RuntimeSpyVeto::None;
    std::vector<RuntimeSpyAction> actions;
    std::vector<RuntimeSpyDiagnostic> diagnostics;
};

struct RuntimeSpyDisguiseState {
    int objectId = -1;
    bool active = false;
    RuntimeSpyDisguiseIdentity identity =
        RuntimeSpyDisguiseIdentity::Infantry;
    std::string disguiseType;
    std::string disguiseOwner;
    // Stock permanent Spy disguises are observer-relative: an opposing
    // player sees the default infantry for their own side.  MakeupKit copies
    // an exact target identity instead and therefore clears this flag.
    bool observerRelative = false;
    int rearmUntilTick = 0;
    int lastChangeTick = 0;
};

struct RuntimeSpyDetectionState {
    int targetObjectId = -1;
    std::string observerOwner;
    int detectorObjectId = -1;
};

struct RuntimeSpyOwnerState {
    std::string owner;
    int blackoutUntilTick = 0;
    bool veteranInfantry = false;
    bool veteranVehicles = false;
    bool veteranAircraft = false;
    int stolenTechMask = 0;
};

struct RuntimeSpyDisguiseResult {
    bool accepted = false;
    bool changed = false;
    RuntimeSpyVeto veto = RuntimeSpyVeto::None;
    std::vector<RuntimeSpyAction> actions;
    std::vector<RuntimeSpyDiagnostic> diagnostics;
};

struct RuntimeSpyDetectionUpdate {
    std::size_t detectedPairs = 0;
    std::vector<RuntimeSpyAction> actions;
};

struct RuntimeSpyReconcileReport {
    std::size_t retainedOwners = 0;
    std::size_t removedOwners = 0;
    std::size_t retainedDisguises = 0;
    std::size_t removedDisguises = 0;
    std::size_t retainedDetections = 0;
    std::size_t removedDetections = 0;
    std::vector<RuntimeSpyAction> actions;
    std::vector<RuntimeSpyDiagnostic> diagnostics;
};

struct RuntimeSpyObjectExitResult {
    bool changed = false;
    std::size_t removedDisguises = 0;
    std::size_t removedDetections = 0;
    std::vector<RuntimeSpyAction> actions;
};

class RuntimeSpySystemState {
public:
    void reset();

    RuntimeSpyInfiltrationResult infiltrate(
        const RuntimeSpyContext& context,
        const RuntimeSpyInfiltrationInput& input
    );

    RuntimeSpyDisguiseResult initializeDefaultDisguise(
        const RuntimeSpyContext& context,
        int objectId,
        RuntimeSpySide side,
        int currentTick
    );

    RuntimeSpyDisguiseResult applyMakesDisguise(
        const RuntimeSpyContext& context,
        int sourceObjectId,
        int targetObjectId,
        const std::string& warheadId,
        int currentTick
    );

    RuntimeSpyDisguiseResult notifyFired(
        const RuntimeSpyContext& context,
        int objectId,
        int currentTick
    );

    RuntimeSpyDisguiseResult notifyDamaged(
        const RuntimeSpyContext& context,
        int objectId,
        int currentTick
    );

    RuntimeSpyDisguiseResult updateStationaryDisguises(
        const RuntimeSpyContext& context,
        int currentTick,
        const std::function<bool(int objectId)>& isStationary
    );

    RuntimeSpyDetectionUpdate updateDetection(
        const RuntimeSpyContext& context,
        int currentTick
    );

    std::vector<RuntimeSpyAction> advance(int currentTick);

    RuntimeSpyReconcileReport reconcile(
        const RuntimeSpyContext& context,
        int currentTick
    );

    // Explicitly forget every object-scoped spy record after a real object
    // exit.  Temporary containment must instead be expressed through
    // RuntimeSpyContext::isObjectRetained.  Repeated notifications are safe
    // and produce no additional actions.
    RuntimeSpyObjectExitResult onObjectExited(
        int objectId,
        int currentTick
    );

    void save(std::ostream& output) const;
    bool load(
        const IniFile& ini,
        const RuntimeSpyContext& context,
        int currentTick,
        std::string* errorMessage = nullptr,
        RuntimeSpyReconcileReport* reconcileReport = nullptr
    );
    std::uint64_t stateHash() const;

    const RuntimeSpyDisguiseState* disguiseState(int objectId) const;
    bool isDisguised(int objectId) const;
    bool isDetectedBy(
        const std::string& observerOwner,
        int targetObjectId
    ) const;
    bool canObserverSeeThroughDisguise(
        const RuntimeSpyContext& context,
        int observerObjectId,
        int targetObjectId
    ) const;
    bool hasVeteranProduction(
        const std::string& owner,
        RuntimeSpyVeteranProduction production
    ) const;
    int blackoutUntilTick(const std::string& owner) const;
    bool hasStolenTechnology(const std::string& owner, RuntimeSpySide side) const;
    bool meetsStolenTechRequirements(const std::string& owner, const UnitType& unit) const;

    const std::vector<RuntimeSpyOwnerState>& ownerStates() const;
    const std::vector<RuntimeSpyDisguiseState>& disguiseStates() const;
    const std::vector<RuntimeSpyDetectionState>& detectionStates() const;
    int lastAdvancedTick() const;

private:
    int lastAdvancedTick_ = -1;
    std::vector<RuntimeSpyOwnerState> owners_;
    std::vector<RuntimeSpyDisguiseState> disguises_;
    std::vector<RuntimeSpyDetectionState> detections_;

    void sortState();
};

const char* runtimeSpyVetoName(RuntimeSpyVeto veto);
const char* runtimeSpyActionName(RuntimeSpyActionKind kind);
const char* runtimeSpyVeteranProductionName(
    RuntimeSpyVeteranProduction production
);
