#if defined(CONCORD_REPLAY_SNAPSHOT_COMPAT_HEADER)
#include "runtime/host/RuntimeShell.h"
#else
#include "runtime/contracts/RuntimeReplayEventSnapshot.h"
#endif
#include "analysis/sandbox/ReplaySandbox.h"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
std::size_t checks = 0;
bool expect(bool condition, const std::string& message) {
    ++checks;
    if (!condition) std::cerr << "Replay snapshot contract failed: " << message << '\n';
    return condition;
}
bool sameCell(const RuntimeSnapshotCell& a, const RuntimeSnapshotCell& b) {
    return a.x == b.x && a.y == b.y;
}
bool sameEvent(const RuntimeReplayEventSnapshot& a, const RuntimeReplayEventSnapshot& b) {
    return a.sequence == b.sequence && a.tick == b.tick && a.kind == b.kind
        && a.objectId == b.objectId && a.targetObjectId == b.targetObjectId && a.projectileActorId == b.projectileActorId
        && a.commandVerb == b.commandVerb && a.selectedCount == b.selectedCount
        && a.acceptedCount == b.acceptedCount && a.rejectedCount == b.rejectedCount
        && sameCell(a.commandTargetCell, b.commandTargetCell) && a.missionFrom == b.missionFrom
        && a.missionTo == b.missionTo && a.reason == b.reason && a.weaponId == b.weaponId
        && a.weaponSlot == b.weaponSlot && a.projectileId == b.projectileId && a.projectileImageId == b.projectileImageId
        && a.warheadId == b.warheadId && a.lifecycleState == b.lifecycleState
        && a.spawnTick == b.spawnTick && a.impactTick == b.impactTick && a.expiredTick == b.expiredTick
        && a.renderIntentId == b.renderIntentId && a.renderIntentHash == b.renderIntentHash
        && a.trailerIntentId == b.trailerIntentId && a.impactIntentId == b.impactIntentId
        && a.warheadImpactIntentId == b.warheadImpactIntentId && sameCell(a.sourceCell, b.sourceCell)
        && sameCell(a.targetCell, b.targetCell) && sameCell(a.currentCell, b.currentCell)
        && a.superWeaponId == b.superWeaponId && a.superWeaponEvent == b.superWeaponEvent
        && sameCell(a.secondTargetCell, b.secondTargetCell);
}
RuntimeReplayEventSnapshot projectile(std::uint64_t sequence, int tick, const std::string& kind,
    const std::string& state, const std::string& renderIntent, std::uint64_t renderHash) {
    RuntimeReplayEventSnapshot e;
    e.sequence = sequence; e.tick = tick; e.kind = kind;
    e.objectId = 7; e.targetObjectId = 8; e.projectileActorId = 9;
    e.weaponId = "120mm"; e.weaponSlot = "Primary"; e.projectileId = "Cannon";
    e.projectileImageId = "120mm"; e.warheadId = "AP"; e.lifecycleState = state;
    e.spawnTick = 2; e.renderIntentId = renderIntent; e.renderIntentHash = renderHash;
    e.sourceCell = {1, 2}; e.targetCell = {4, 5};
    return e;
}
std::vector<RuntimeReplayEventSnapshot> expectedFixture() {
    std::vector<RuntimeReplayEventSnapshot> events(6);
    auto& command = events[0];
    command.sequence = 1; command.tick = 0; command.kind = "CommandAccepted";
    command.objectId = 7; command.targetObjectId = 8; command.commandVerb = "Attack";
    command.selectedCount = 2; command.acceptedCount = 1; command.rejectedCount = 1;
    command.commandTargetCell = {4, 5};
    auto& mission = events[1];
    mission.sequence = 2; mission.tick = 1; mission.kind = "MissionTransition";
    mission.objectId = 7; mission.missionFrom = "Guard"; mission.missionTo = "Attack"; mission.reason = "S12Fixture";
    events[2] = projectile(3, 2, "ProjectileSpawned", "Flying", "S12Trail", 42);
    events[2].trailerIntentId = "S12Trail"; events[2].currentCell = {2, 3};
    events[3] = projectile(4, 3, "ProjectileImpact", "Impact", "S12Impact", 43);
    events[3].impactTick = 3; events[3].impactIntentId = "S12Impact";
    events[3].warheadImpactIntentId = "S12Warhead"; events[3].currentCell = {4, 5};
    events[4] = projectile(5, 4, "ProjectileExpired", "Expired", "S12Expired", 44);
    events[4].impactTick = 3; events[4].expiredTick = 4; events[4].impactIntentId = "S12Impact";
    events[4].currentCell = {4, 5};
    // Existing loader turns later sequence=0 into the current vector size (5),
    // and ignores the three superweapon-related keys present in the fixture.
    events[5].sequence = 5; events[5].tick = 5; events[5].kind = "SuperWeapon";
    return events;
}
bool emptySummary(const ReplayRuntimeEventSourceSummary& s) {
    return s.eventCount == 0 && s.runtimeEventCount == 0 && s.sandboxFallbackCount == 0
        && s.commandAcceptedCount == 0 && s.missionTransitionCount == 0 && s.projectileSpawnCount == 0
        && s.projectileImpactCount == 0 && s.projectileExpiredCount == 0 && s.renderIntentCount == 0
        && s.projectileLifecycle.count == 0 && s.projectileLifecycle.runtimeSourceCount == 0
        && s.projectileLifecycle.sandboxFallbackCount == 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Pass the valid and invalid replay event fixture paths.\n";
        return 2;
    }
    bool ok = true;
    const std::string validPath(argv[1]), invalidPath(argv[2]);
    std::vector<RuntimeReplayEventSnapshot> events(1);
    events[0].kind = "stale";
    std::string error = "previous diagnostic";
    const bool loaded = loadRuntimeReplayEventSnapshotsFromFile(validPath, events, error);
    ok &= expect(loaded && error.empty(), "actual production parser rejected valid input: " + error);
    const auto expected = expectedFixture();
    ok &= expect(events.size() == expected.size(), "load must replace old state with exactly six fixture events");
    if (events.size() == expected.size()) {
        for (std::size_t index = 0; index < expected.size(); ++index) {
            ok &= expect(sameEvent(events[index], expected[index]), "parsed member/default mismatch at event " + std::to_string(index));
        }
    }
    if (!loaded || events.size() != expected.size()) return 1;
    const auto summary = summarizeRuntimeReplayEventSnapshots(events.data(), events.size());
    ok &= expect(summary.eventCount == 6 && summary.runtimeEventCount == 6 && summary.sandboxFallbackCount == 0
        && summary.commandAcceptedCount == 1 && summary.missionTransitionCount == 1
        && summary.projectileSpawnCount == 1 && summary.projectileImpactCount == 1 && summary.projectileExpiredCount == 1
        && summary.renderIntentCount == 3 && summary.projectileLifecycle.count == 3
        && summary.projectileLifecycle.runtimeSourceCount == 3 && summary.projectileLifecycle.sandboxFallbackCount == 0,
        "production summary counts or complete-runtime-field classification changed");
    const auto empty = summarizeRuntimeReplayEventSnapshots(nullptr, 0);
    const auto nullWithCount = summarizeRuntimeReplayEventSnapshots(nullptr, 7);
    const std::vector<ReplayProjectileLifecycleRecord> noLifecycle;
    const auto emptyLifecycle = summarizeProjectileLifecycleRecords(noLifecycle);
    ok &= expect(emptySummary(empty) && emptySummary(nullWithCount)
        && !empty.projectileLifecycle.hash.empty() && empty.projectileLifecycle.hash == nullWithCount.projectileLifecycle.hash
        && empty.projectileLifecycle.hash == emptyLifecycle.hash,
        "null/zero event summary or shared empty lifecycle digest changed");
    ok &= expect(!summary.projectileLifecycle.hash.empty()
        && summary.projectileLifecycle.hash != empty.projectileLifecycle.hash, "nonempty lifecycle disappeared from hash");
    auto changed = events;
    ++changed[3].renderIntentHash;
    const auto changedSummary = summarizeRuntimeReplayEventSnapshots(changed.data(), changed.size());
    ok &= expect(changedSummary.projectileLifecycle.hash != summary.projectileLifecycle.hash,
        "actual summary no longer hashes render-intent identity");
    RuntimeReplayEventSnapshot sparse;
    sparse.tick = 9; sparse.kind = "ProjectileSpawned";
    const auto partial = summarizeRuntimeReplayEventSnapshots(&sparse, 1);
    ok &= expect(partial.eventCount == 1 && partial.sandboxFallbackCount == 1
        && partial.projectileLifecycle.count == 1 && partial.projectileLifecycle.runtimeSourceCount == 1
        && partial.projectileLifecycle.sandboxFallbackCount == 1,
        "existing runtime-partial classification changed");

    const bool rejected = loadRuntimeReplayEventSnapshotsFromFile(invalidPath, events, error);
    ok &= expect(!rejected && events.empty()
        && error == "Runtime replay event file " + invalidPath + " line 2: invalid value for key tick",
        "invalid later line must clear partial state and retain the old exact error: " + error);
    const std::string missingPath = invalidPath + ".missing";
    std::error_code fileError;
    ok &= expect(!std::filesystem::exists(missingPath, fileError) && !fileError,
        "missing-file fixture path unexpectedly exists or cannot be checked");
    events = expected;
    const bool missing = loadRuntimeReplayEventSnapshotsFromFile(missingPath, events, error);
    ok &= expect(!missing && events.empty() && error == "Failed to open runtime replay event file: " + missingPath,
        "missing-file load must clear prior state with the original error");
    if (!ok) return 1;
    // Root captures this exact output before/after extraction under each matching
    // ABI profile. The hash baseline is measured, never invented by this test.
    std::cout << "Replay snapshot contract passed checks=" << checks
        << " events=" << summary.eventCount << " accepted=" << summary.commandAcceptedCount
        << " missions=" << summary.missionTransitionCount << " spawn=" << summary.projectileSpawnCount
        << " impact=" << summary.projectileImpactCount << " expired=" << summary.projectileExpiredCount
        << " render=" << summary.renderIntentCount << " lifecycle=" << summary.projectileLifecycle.hash
        << " emptyLifecycle=" << empty.projectileLifecycle.hash
        << " invalid=atomic-clear ignored-super-fields=defaults zero-sequence=existing-index\n";
    return 0;
}
