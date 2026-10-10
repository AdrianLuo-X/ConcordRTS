#pragma once

#include "world/model/GameWorld.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

class IniFile;
class RulesDatabase;

enum class RuntimeDeathWeaponResolution : std::uint8_t {
    Explicit = 0,
    GlobalDefault = 1
};

enum class RuntimeDeathWeaponVeto : std::uint8_t {
    None = 0,
    InvalidContext,
    ObjectMissing,
    ObjectAlreadyTriggered,
    SuppressedByMechanism,
    TypeMissing,
    NotConfigured,
    WeaponMissing,
    InvalidDamageModifier,
    InvalidCascadeCause,
    CascadeDepthExceeded,
    RequestSequenceExhausted
};

// A cause is copied from a fire request when that request kills another
// object.  It deliberately contains no pointers or callbacks, so a host can
// queue, replay, or serialize the surrounding damage event safely.
struct RuntimeDeathWeaponCause {
    std::uint64_t cascadeId = 0;
    std::uint64_t parentRequestId = 0;
    std::uint32_t parentDepth = 0;
};

struct RuntimeDeathWeaponDeathEvent {
    int objectId = -1;
    int currentTick = 0;
    RuntimeDeathWeaponCause cause;
    // Some authoritative deaths (for example, a parasite destroyed while
    // attached to its host) deliberately suppress DeathWeapon.  The death is
    // still consumed by the once-only triggered-object record, so a later
    // duplicate notification cannot retroactively emit the weapon.
    bool suppressFire = false;
};

// This is the complete value-only contract consumed by the normal weapon
// firing path.  Source attribution and the impact origin always come from the
// object that died, never from the object that caused its death.
struct RuntimeDeathWeaponFireRequest {
    std::uint64_t requestId = 0;
    std::uint64_t cascadeId = 0;
    std::uint64_t parentRequestId = 0;
    std::uint32_t cascadeDepth = 0;
    int currentTick = 0;

    int sourceObjectId = -1;
    WorldObjectKind sourceKind = WorldObjectKind::Structure;
    bool sourceAuthoredAircraft = false;
    std::string sourceOwner;
    std::string sourceType;
    std::string sourceMapId;
    std::string sourceTag;
    int x = -1;
    int y = -1;
    int cell = -1;
    int level = 0;
    bool onBridge = false;

    std::string weaponId;
    RuntimeDeathWeaponResolution resolution =
        RuntimeDeathWeaponResolution::Explicit;
    int authoredDamage = 0;
    double damageModifier = 1.0;
    int modifiedDamage = 0;
};

struct RuntimeDeathWeaponResult {
    // accepted means the authoritative death was recorded.  An accepted
    // result may intentionally have no fire request (for example, no authored
    // DeathWeapon or a cascade that reached its depth limit).
    bool accepted = false;
    RuntimeDeathWeaponVeto veto = RuntimeDeathWeaponVeto::None;
    std::optional<RuntimeDeathWeaponFireRequest> fireRequest;
};

struct RuntimeDeathWeaponContext {
    const RulesDatabase* rules = nullptr;
    const std::vector<WorldObjectInstance>* objects = nullptr;
    // Depth zero is the first death weapon.  A value of 16 therefore permits
    // depths 0 through 15 and suppresses the next recursive emission.
    std::uint32_t maximumCascadeDepth = 16;
};

struct RuntimeDeathWeaponTriggeredObject {
    // Presence is the authoritative once-only tombstone, including deaths
    // suppressed by a mechanism.  emittedRequestId remains zero whenever no
    // fire request was emitted, so suppression needs no new save field.
    int objectId = -1;
    WorldObjectKind kind = WorldObjectKind::Structure;
    bool authoredAircraft = false;
    std::string type;
    int deathTick = 0;
    std::uint64_t emittedRequestId = 0;
};

struct RuntimeDeathWeaponReconcileReport {
    std::size_t retained = 0;
    std::size_t removedMissingObjects = 0;
    std::size_t removedIdentityMismatches = 0;
};

class RuntimeDeathWeaponSystemState {
public:
    void reset();

    RuntimeDeathWeaponResult notifyDeath(
        const RuntimeDeathWeaponContext& context,
        const RuntimeDeathWeaponDeathEvent& event
    );

    RuntimeDeathWeaponReconcileReport reconcile(
        const RuntimeDeathWeaponContext& context
    );

    void save(std::ostream& output) const;
    bool load(
        const IniFile& ini,
        const RuntimeDeathWeaponContext& context,
        std::string* errorMessage = nullptr,
        RuntimeDeathWeaponReconcileReport* reconcileReport = nullptr
    );
    std::uint64_t stateHash() const;

    bool hasTriggered(int objectId) const;
    std::uint64_t nextRequestId() const;
    const std::vector<RuntimeDeathWeaponTriggeredObject>& triggeredObjects()
        const;

private:
    std::uint64_t nextRequestId_ = 1;
    std::vector<RuntimeDeathWeaponTriggeredObject> triggeredObjects_;

    void sortState();
};

RuntimeDeathWeaponCause runtimeDeathWeaponCauseFrom(
    const RuntimeDeathWeaponFireRequest& request
);
const char* runtimeDeathWeaponVetoName(RuntimeDeathWeaponVeto veto);
const char* runtimeDeathWeaponResolutionName(
    RuntimeDeathWeaponResolution resolution
);
