#pragma once

#include <cstdint>
#include <string>

struct RuntimeSnapshotCell {
    int x = -1;
    int y = -1;
};

struct RuntimeReplayEventSnapshot {
    std::uint64_t sequence = 0;
    int tick = 0;
    std::string kind;
    int objectId = -1;
    int targetObjectId = -1;
    int projectileActorId = -1;
    std::string commandVerb;
    int selectedCount = 0;
    int acceptedCount = 0;
    int rejectedCount = 0;
    RuntimeSnapshotCell commandTargetCell;
    std::string missionFrom;
    std::string missionTo;
    std::string reason;
    std::string weaponId;
    std::string weaponSlot;
    std::string projectileId;
    std::string projectileImageId;
    std::string warheadId;
    std::string lifecycleState;
    int spawnTick = -1;
    int impactTick = -1;
    int expiredTick = -1;
    std::string renderIntentId;
    std::uint64_t renderIntentHash = 0;
    std::string trailerIntentId;
    std::string impactIntentId;
    std::string warheadImpactIntentId;
    RuntimeSnapshotCell sourceCell;
    RuntimeSnapshotCell targetCell;
    RuntimeSnapshotCell currentCell;
    std::string superWeaponId;
    std::string superWeaponEvent;
    RuntimeSnapshotCell secondTargetCell;
};
