#pragma once

#include "data/rules/RulesDatabase.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct RuntimeReplayEventSnapshot;

struct ReplaySandboxConfig {
    std::string attackerId = "MTNK";
    std::string defenderId = "HTNK";
    int maxTicks = 600;
    int startDistanceCells = 8;
    int randomSeed = 12345;
    int playerCount = 2;
    int desyncAtTick = -1;
    std::string outputPath;
    bool includeCapabilityDetails = false;
    const RuntimeReplayEventSnapshot* runtimeEventSnapshots = nullptr;
    std::size_t runtimeEventSnapshotCount = 0;
};

struct ReplayCommandRecord {
    int tick = 0;
    int playerId = 0;
    int sequence = 0;
    std::string type;
    int subjectInstanceId = 0;
    int targetInstanceId = 0;
};

struct ReplayHashFrame {
    int tick = 0;
    int commandsApplied = 0;
    std::vector<std::string> peerHashes;
};

struct ReplaySandboxEvent {
    int tick = 0;
    std::string text;
};

struct ReplayCapabilityIntentRecord {
    int tick = 0;
    int playerId = 0;
    int sequence = 0;
    int subjectInstanceId = 0;
    int targetInstanceId = 0;
    std::string selectedCapabilityHash;
    std::string abilityType;
    std::string weaponSlot;
    std::string weaponId;
    int projectileActorId = 0;
    std::string projectileId;
    std::string projectileImageId;
    std::string warheadId;
    std::string missionBefore;
    std::string missionAfter;
    std::string pathIntentHash;
    std::string renderIntentHash;
    std::string trailerId;
    std::string impactId;
    std::string pathRequestAbilityType;
    std::string passabilityProfileHash;
    std::string pathFallbackMode;
    std::string pathDiagnosticHash;
    std::string chainDiagnosticHash;
    std::string chainFieldSource = "sandbox-fallback";
    int projectileSpawnTick = -1;
    int impactTick = -1;
    int expiredTick = -1;
    std::string renderIntentId;
    bool commandAccepted = false;
    bool missionChanged = false;
    bool weaponSelected = false;
    bool projectileSpawned = false;
    bool impactResolved = false;
    bool pathQueued = false;
    bool pathCacheHit = false;
    bool pathCacheMiss = false;
    bool pathBudgetExhausted = false;
    bool footprintRingFallback = false;
    bool renderIntentExpected = false;
    bool runtimeChainFieldsAvailable = false;
    bool sandboxFallbackFields = true;
};

struct ReplayProjectileLifecycleRecord {
    int projectileActorId = 0;
    int ownerInstanceId = 0;
    int targetInstanceId = 0;
    int spawnTick = -1;
    int impactTick = -1;
    int expiredTick = -1;
    std::string weaponSlot;
    std::string weaponId;
    std::string projectileId;
    std::string projectileImageId;
    std::string warheadId;
    std::string impactId;
    std::string renderIntentHash;
    std::string renderIntentId;
    std::string fieldSource = "sandbox-fallback";
    bool runtimeFieldsAvailable = false;
    bool sandboxFallbackFields = true;
};

struct ReplayProjectileLifecycleSummary {
    int count = 0;
    int runtimeSourceCount = 0;
    int sandboxFallbackCount = 0;
    std::string hash;
};

struct ReplayRuntimeEventSourceSummary {
    int eventCount = 0;
    int runtimeEventCount = 0;
    int sandboxFallbackCount = 0;
    int commandAcceptedCount = 0;
    int missionTransitionCount = 0;
    int projectileSpawnCount = 0;
    int projectileImpactCount = 0;
    int projectileExpiredCount = 0;
    int renderIntentCount = 0;
    ReplayProjectileLifecycleSummary projectileLifecycle;
};

struct ReplaySandboxReport {
    bool success = false;
    std::string errorMessage;
    std::string attackerId;
    std::string defenderId;
    int playerCount = 0;
    int randomSeed = 0;
    int ticksSimulated = 0;
    int commandCount = 0;
    std::string commandLogHash;
    std::string commandIntentHash;
    std::string attackPathIntentHash;
    bool commandIntentValidated = false;
    bool attackPathIntentValidated = false;
    int pathIntentSamples = 0;
    int pathFallbackTargetsTried = 0;
    int pathCacheHits = 0;
    int pathCacheMisses = 0;
    bool pathBudgetExhausted = false;
    bool replayVerified = false;
    bool desyncDetected = false;
    bool expectedDesync = false;
    int firstDesyncTick = -1;
    std::string desyncSummary;
    std::string finalStateHash;
    std::string winner;
    bool replayExported = false;
    std::string replayOutputPath;
    std::vector<std::string> lockstepRules;
    std::vector<ReplayCommandRecord> commands;
    std::vector<ReplayHashFrame> hashFrames;
    std::vector<ReplaySandboxEvent> events;
    std::string capabilityIntentHash;
    bool capabilityIntentValidated = false;
    bool deployIntentStable = false;
    bool attackIntentStable = false;
    bool projectileIntentStable = false;
    bool pathDiagnosticsStable = false;
    bool commandMissionProjectileChainStable = false;
    int capabilityIntentCount = 0;
    int deployIntentCount = 0;
    int attackIntentCount = 0;
    int projectileIntentCount = 0;
    int commandAcceptedCount = 0;
    int missionChangedCount = 0;
    int weaponSelectedCount = 0;
    int projectileSpawnedCount = 0;
    int impactResolvedCount = 0;
    int pathQueuedCount = 0;
    int renderIntentExpectedCount = 0;
    int capabilityRuntimeFieldCount = 0;
    int capabilitySandboxFallbackFieldCount = 0;
    int projectileLifecycleCount = 0;
    int projectileLifecycleRuntimeCount = 0;
    int projectileLifecycleFallbackCount = 0;
    std::string projectileLifecycleHash;
    ReplayRuntimeEventSourceSummary runtimeEventSource;
    std::string runtimeSourceEvidence;
    std::string firstDesyncClass;
    int capabilityHashFrameCount = 0;
    int totalHashFrameCount = 0;
    int replayPathRequestCount = 0;
    bool capabilityDetailsIncluded = false;
    std::vector<ReplayCapabilityIntentRecord> capabilityIntents;
};

ReplayProjectileLifecycleSummary summarizeProjectileLifecycleRecords(
    const std::vector<ReplayProjectileLifecycleRecord>& records
);

ReplayRuntimeEventSourceSummary summarizeRuntimeReplayEventSnapshots(
    const RuntimeReplayEventSnapshot* events,
    std::size_t count
);

ReplaySandboxReport runReplaySandbox(
    const RulesDatabase& rules,
    const ReplaySandboxConfig& config
);

ReplaySandboxReport runReplaySandboxWithRuntimeEvents(
    const RulesDatabase& rules,
    ReplaySandboxConfig config,
    const RuntimeReplayEventSnapshot* runtimeEvents,
    std::size_t runtimeEventCount
);

ReplaySandboxReport runReplaySandboxWithRuntimeEvents(
    const RulesDatabase& rules,
    ReplaySandboxConfig config,
    const std::vector<RuntimeReplayEventSnapshot>& runtimeEvents
);

bool loadRuntimeReplayEventSnapshotsFromFile(
    const std::string& path,
    std::vector<RuntimeReplayEventSnapshot>& runtimeEvents,
    std::string& errorMessage
);

ReplaySandboxReport runReplaySandboxWithRuntimeEventFile(
    const RulesDatabase& rules,
    ReplaySandboxConfig config,
    const std::string& runtimeEventPath,
    std::string* errorMessage = nullptr
);
