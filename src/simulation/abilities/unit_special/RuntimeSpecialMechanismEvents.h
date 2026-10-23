#pragma once

#include <functional>
#include <string>

struct SpecialCell {
    int x = -1;
    int y = -1;
};

// The shell's existing terrain targets use -(base + y * 1000 + x).
// Only these bounded cell IDs, positive object IDs, and -1 (no target)
// are accepted by the carrier state machine.
bool decodeRuntimeCarrierCellTarget(int targetId, SpecialCell& cell, bool& bridge);

struct SpecialMechanismEvent {
    std::string kind;
    int tick = 0;
    int objectId = -1;
    int targetObjectId = -1;
    int value = 0;
    SpecialCell cell;
    std::string reason;
};

enum class SpecialMechanismActionKind {
    TimedBombDetonate,
    ParasiteEnterLimbo,
    ParasiteAttachHost,
    ParasiteReturnFromLimbo,
    ParasiteKillTarget,
    ParasiteDamage,
    ParasiteEject,
    ParasiteKillParasite,
    ParasiteReleaseTarget
};

// RuntimeSpecialMechanismState never owns or mutates the referenced objects
// for these mechanisms.  Callers consume these value-only actions at a safe
// point in their world update.
struct SpecialMechanismAction {
    SpecialMechanismActionKind kind =
        SpecialMechanismActionKind::TimedBombDetonate;
    int tick = 0;
    int projectileId = -1;
    int objectId = -1;
    int targetObjectId = -1;
    std::string owner;
    int damage = 0;
    std::string warheadId;
    bool forceDestruction = false;
    int paralyzes = 0;
    bool culling = false;
    bool suppressDeathWeapon = false;
    int stunTicks = 0;
    SpecialCell cell;
    std::string reason;
    // Transient placement permission for this parasite's own lethal bite or
    // infantry cull. Other host events retain ordinary occupied-cell checks.
    bool replacesTerminatingHost = false;
    // Audio provenance only: the source was already inside this host, rather
    // than still travelling toward a pending launch target. Not persisted.
    bool sourceWasAttachedToHost = false;
};

using SpecialEventSink = std::function<void(const SpecialMechanismEvent&)>;
