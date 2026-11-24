#pragma once
#include "simulation/combat/damage/RuntimeDeathWeaponSystem.h"
#include "audio/spatial/RuntimeSpatialAudio.h"
#include <cstdint>
#include <optional>
#include <string>

// Preserve the existing C++ type identity while sharing only owned values.
namespace concord::runtime::session_detail {
enum class RuntimeObjectExitKind : std::uint8_t {
    Destroyed = 0,
    TemporalDestroyed,
    Sold,
    Consumed,
    TransformSource,
    ScriptRemoved
};

struct RuntimeObjectExitRequest {
    int objectId = -1;
    RuntimeObjectExitKind kind = RuntimeObjectExitKind::Destroyed;
    int tick = -1;
    std::string mission;
    std::string reason;
    int sourceObjectId = -1;
    std::string sourceOwner;
    RuntimeDeathWeaponCause deathWeaponCause;
    bool suppressDeathWeapon = false;
    bool destructionEffectsSpawned = false;
    // Local presentation only; pending exits are drained before save/hash.
    bool unitDeathSoundRequested = false;
    bool unitDeathSoundHandled = false;
    std::optional<RuntimeSpatialAudioPosition> unitDeathSoundPosition;
    bool suppressCrew = false;
    bool c4Explosion = false;
    std::string warheadId;
    // Module damage callbacks can mark the victim inactive before the host
    // drains its exit. Preserve that it was alive for this lethal hit.
    bool wasAliveBeforeDamage = false;
};

enum class RuntimeObjectExitQueueResult : std::uint8_t {
    Queued = 0,
    DuplicatePending,
    AlreadyFinalized,
    ObjectMissing
};
}
