#pragma once

#include "world/model/GameWorld.h"
#include "data/rules/RulesDatabase.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

class IniFile;

enum class RuntimeGrindingRelationPhase {
    Queued,
    Entering
};

enum class RuntimeGrindingVeto {
    None,
    InvalidContext,
    RelationCapacityReached,
    HostMissing,
    HostUnavailable,
    HostNotGrinder,
    HostUnpowered,
    SourceMissing,
    SourceUnavailable,
    SourceNotGroundTechno,
    SourceEngineer,
    SourceFlying,
    SourceChrono,
    SourceNaval,
    SourceTransported,
    SourceGarrisoned,
    SourceAlliedNotOwned,
    SourceEnemy,
    SourceAlreadyManaged,
    SourceHasNoRefund,
    PassengerManifestInvalid,
    PassengerAlreadyManaged,
    RelationMissing,
    RelationMismatch,
    EntryInactive,
    SourceNotInsideFootprint
};

enum class RuntimeGrindingActionKind {
    MoveToHost,
    RemoveUnit,
    Release
};

enum class RuntimeGrindingRemovalRole {
    None,
    Passenger,
    Root
};

enum class RuntimeGrindingTerminalDisposition {
    None,
    ConsumeInsideGrinder
};

// Every action is value-only. RemoveUnit actions sharing rootSourceId form one
// atomic batch; passengers are ordered by object id and the transport itself
// is last so a host can consume every passenger before consuming the root
// without triggering a container-destruction cascade.  Every RemoveUnit action
// explicitly carries its batch role and persistent terminal disposition; a
// host must not infer either from reason text or object ordering alone.
struct RuntimeGrindingAction {
    RuntimeGrindingActionKind kind = RuntimeGrindingActionKind::Release;
    RuntimeGrindingRemovalRole removalRole =
        RuntimeGrindingRemovalRole::None;
    RuntimeGrindingTerminalDisposition terminalDisposition =
        RuntimeGrindingTerminalDisposition::None;
    int hostId = -1;
    int rootSourceId = -1;
    int objectId = -1;
    int batchIndex = 0;
    int batchSize = 0;
    RuntimeGrindingRelationPhase relationPhase =
        RuntimeGrindingRelationPhase::Queued;
    std::string owner;
    std::string objectType;
    int purchaseValue = 0;
    int currentValue = 0;
    int refundCredits = 0;
    std::string reason;
};

struct RuntimeGrindingCommandResult {
    bool accepted = false;
    RuntimeGrindingVeto veto = RuntimeGrindingVeto::None;
    int hostId = -1;
    int sourceId = -1;
    std::string reason;
    std::vector<RuntimeGrindingAction> actions;
};

struct RuntimeGrindingRelation {
    int hostId = -1;
    int sourceId = -1;
    RuntimeGrindingRelationPhase phase =
        RuntimeGrindingRelationPhase::Queued;
    std::string hostOwner;
    std::string sourceOwner;
    std::string hostType;
    std::string sourceType;
    int queuedTick = 0;
    int enteringTick = -1;
    std::vector<int> passengerIds;
};

struct RuntimeGrindingContext {
    const RulesDatabase* rules = nullptr;
    const std::vector<WorldObjectInstance>* objects = nullptr;

    std::function<std::string(const std::string&)> canonicalOwnerId;
    std::function<bool(const std::string&, const std::string&)> areAllied;
    std::function<bool(int hostId, int sourceId)> isEntryActive;
    std::function<bool(int hostId, int sourceId)> isInsideHostFootprint;
    std::function<int(const WorldObjectInstance&)> purchaseValue;
    std::function<int(const WorldObjectInstance&)> currentValue;
};

struct RuntimeGrindingReconcileReport {
    bool contextValid = false;
    std::size_t retainedRelations = 0;
    std::size_t releasedRelations = 0;
    std::size_t duplicateManagedObjects = 0;
    std::vector<RuntimeGrindingAction> actions;
};

class RuntimeGrindingSystemState {
public:
    RuntimeGrindingSystemState() = default;

    void reset();

    RuntimeGrindingCommandResult queueEntry(
        const RuntimeGrindingContext& context,
        int hostId,
        int sourceId,
        int currentTick
    );
    RuntimeGrindingCommandResult beginEnter(
        const RuntimeGrindingContext& context,
        int hostId,
        int sourceId,
        int currentTick
    );
    // A repeated order to the current host must not requeue or reset entry.
    bool hasActiveEntry(
        const RuntimeGrindingContext& context, int hostId, int sourceId
    ) const;
    std::vector<RuntimeGrindingAction> cancelSource(
        int sourceId,
        const std::string& reason
    );

    std::vector<RuntimeGrindingAction> tick(
        const RuntimeGrindingContext& context,
        int currentTick
    );
    RuntimeGrindingReconcileReport reconcile(
        const RuntimeGrindingContext& context
    );

    void save(std::ostream& output) const;
    bool load(
        const IniFile& ini,
        const RuntimeGrindingContext& context,
        std::string* errorMessage = nullptr,
        RuntimeGrindingReconcileReport* reconcileReport = nullptr
    );
    std::uint64_t stateHash() const;

    const std::vector<RuntimeGrindingRelation>& relations() const;
    int lastAdvancedTick() const;

private:
    int lastAdvancedTick_ = -1;
    std::vector<RuntimeGrindingRelation> relations_;

    void sortState();
};

const char* runtimeGrindingVetoName(RuntimeGrindingVeto veto);
const char* runtimeGrindingActionName(RuntimeGrindingActionKind kind);
