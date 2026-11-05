#pragma once

#include "world/values/RuntimeCellPoint.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_set>
#include <vector>

// Original owned movement command values, shared by live simulation and restore.
enum class RuntimeBehaviorKind {
    None,
    Move,
    Attack
};

enum class RuntimeBehaviorState {
    Idle,
    Queued,
    Moving,
    Blocked,
    Chasing,
    Attacking,
    Cooldown,
    Complete
};

struct RuntimeRepackMoveGroupIntent {
    RuntimeCellPoint center;
    // Full identities survive a peer's folding phase; only mobile members
    // are projected into the path request's soft occupancy exclusions.
    std::vector<int> members;
};

enum class RuntimeTransportTransferKind { None, Boarding, Egress };

struct RuntimeObjectBehavior {
    RuntimeBehaviorKind kind = RuntimeBehaviorKind::None;
    RuntimeBehaviorState state = RuntimeBehaviorState::Idle;
    RuntimeCellPoint moveTarget;
    int targetObjectId = -1;
    bool autoAcquired = false;
    bool forceFire = false;
    bool attackGround = false;
    bool attackBridge = false;
    // Fixed terrain target shared by bridge attacks and ground force-fire.
    RuntimeCellPoint attackBridgeCell;
    std::deque<RuntimeCellPoint> path;
    bool pathRequestPending = false;
    bool pendingPathTowardRange = false;
    RuntimeCellPoint pendingPathStart;
    RuntimeCellPoint pendingPathGoal;
    bool pendingHasSharedGoal = false;
    RuntimeCellPoint pendingSharedGoal;
    int pendingAllowedOccupiedObjectId = -1;
    int pendingTargetObjectId = -1;
    double pendingRangeCells = 0.0;
    bool pendingAllowNearbyTarget = false;
    std::unordered_set<int> pendingIgnoredObjectIds;
    std::optional<RuntimeRepackMoveGroupIntent> repackMoveGroup;
    int moveCooldownTicks = 0;
    // Authoritative departure cell, held until a movement segment finishes.
    // Rendering anchors may be reset independently or omitted on load.
    RuntimeCellPoint moveSegmentFrom{-1, -1};
    std::int32_t moveIntervalRemainderQ16 = 0;
    std::int64_t moveStartupDistanceQ16 = 0;
    double renderStartupAccelerationDistance = 0.0;
    // Explicit provenance of a fresh automatic factory yield, never a player order.
    int factoryExitClearanceHostId = -1;
    // Immutable presentation provenance for one already accepted adjacent cargo transfer.
    int transportTransferHostId = -1;
    RuntimeTransportTransferKind transportTransferKind = RuntimeTransportTransferKind::None;
    int weaponCooldownTicks = 0;
    int blockedTicks = 0;
    int pathReplans = 0;
    int nextRepathTick = 0;
    bool returnToDeployed = false;
    int approachSlotIndex = 0;
    RuntimeCellPoint renderFrom;
    RuntimeCellPoint renderTo;
    double renderFromOffsetX = 0.0, renderFromOffsetY = 0.0;
    double renderToOffsetX = 0.0, renderToOffsetY = 0.0;
    double renderFromLevel = 0.0;
    double renderToLevel = 0.0;
    bool renderFromOnBridge = false;
    bool renderToOnBridge = false;
    int renderMoveTicksTotal = 0;
    int renderMoveTicksRemaining = 0;
    // Derived segment presentation; the adjusted cooldown is authoritative.
    double renderArrivalDistanceAfter = -1.0;
};
