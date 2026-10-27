#pragma once

#include "simulation/navigation/pathfinding/PathGoalCandidates.h"
#include "core/tasks/BoundedTaskExecutor.h"

#include <array>
#include <cstdint>
#include <optional>

// All fields crossing the executor boundary are values. No terrain view,
// request span, queue, component cache, World, RNG or resource pointer survives.
struct PathGoalTaskStamp {
    std::uint64_t generation = 1;
    std::uint64_t sequence = 0;
    std::uint64_t mapRevision = 0;
    std::uint64_t occupancyRevision = 0;
    std::int64_t simulationTick = -1;
    int queueTick = 0;
    int requestId = -1;
    int searchBudget = 0; // Observed only; this kernel consumes zero search nodes.
    bool operator==(const PathGoalTaskStamp&) const = default;
};

struct PathGoalTaskCell {
    PathCell cell;
    // present, passable, cliff, water, bridge, shore; no mutable lookup/cache.
    std::uint32_t flags = 0;
};

struct PathGoalTaskInput {
    static constexpr std::size_t MaxTargets = 64;
    static constexpr std::size_t MaxNearby = 25;
    PathGoalTaskStamp stamp;
    PathCell start;
    PathCell goal;
    PathMovementMode movementMode = PathMovementMode::Ground;
    int goalSlotCount = 0;
    bool mapQueryApproach = false;
    std::uint32_t targetCount = 0;
    std::uint32_t nearbyCount = 0;
    PathGoalTaskCell goalCell;
    std::array<PathGoalTaskCell, MaxTargets> targets{};
    // Authored index order, before the original first-16 truncation.
    std::array<PathGoalTaskCell, MaxNearby> nearby{};
};

struct PathGoalTaskOutput {
    PathGoalTaskStamp stamp;
    std::uint32_t count = 0;
    bool mapQueryApproachUsed = false;
    bool requiresFallbackRing = false;
    std::uint32_t searchNodesConsumed = 0;
    std::array<PathCell, PathGoalTaskInput::MaxTargets> cells{};
    std::uint64_t computeNanoseconds = 0; // Diagnostic; never a decision input.
};

std::optional<PathGoalTaskInput> capturePathGoalTaskInput(
    const PathGoalCandidateInput&, const PathGoalTerrainView&, PathGoalTaskStamp);
PathGoalTaskOutput computePathGoalTask(const PathGoalTaskInput&,
    std::span<PathCell> privateScratch);
std::size_t pathGoalTaskKernel(const concord::tasks::JobContext&);
concord::tasks::Task makePathGoalTask(const PathGoalTaskInput&);
std::optional<PathGoalTaskOutput> takePathGoalTask(
    concord::tasks::BoundedTaskExecutor&, concord::tasks::JobKey,
    const PathGoalTaskStamp& expected);
bool pathGoalTaskEquals(const PathGoalTaskOutput&, const PathGoalCandidateSet&);

// The consumer validates the complete identity again. Missing/rejected output
// is recomputed from the same owned input with independent owner scratch.
PathGoalCandidateSet adoptPathGoalTask(const PathGoalTaskInput&,
    const std::optional<PathGoalTaskOutput>&, bool& usedWorker);
enum class PathGoalCandidateMode { Shadow, Parallel };

struct PathGoalCandidateTaskStats {
    std::uint64_t captured = 0;
    std::uint64_t submitted = 0;
    std::uint64_t compared = 0;
    std::uint64_t unavailable = 0;
    std::uint64_t mismatches = 0;
    std::uint64_t adopted = 0;
    std::uint64_t synchronousFallbacks = 0;
    double snapshotMs = 0;
    double enqueueMs = 0;
    double computeMs = 0;
    double waitMs = 0;
    double compareMs = 0;
};

// A session-owned sidecar. generate() fully consumes each singleton task at
// the original decision point. Requests with sequential cache/budget effects
// are never speculatively batched. Shadow returns the original oracle; parallel
// adopts this same kernel or recomputes from the identical owned input.
class PathGoalCandidateRunner final {
public:
    explicit PathGoalCandidateRunner(std::size_t workers, bool report = false,
        concord::tasks::ExecutorConfig limits = {},
        PathGoalCandidateMode mode = PathGoalCandidateMode::Shadow);
    ~PathGoalCandidateRunner();
    PathGoalCandidateRunner(const PathGoalCandidateRunner&) = delete;
    PathGoalCandidateRunner& operator=(const PathGoalCandidateRunner&) = delete;
    PathGoalCandidateSet generate(const PathGoalCandidateInput&,
        const PathGoalTerrainView&, PathGoalTaskStamp);
    const PathGoalCandidateTaskStats& stats() const noexcept { return stats_; }
    concord::tasks::ExecutorStats executorStats() const { return executor_.stats(); }
private:
    std::size_t workers_;
    bool report_;
    PathGoalCandidateMode mode_;
    std::uint64_t sequence_ = 0;
    PathGoalCandidateTaskStats stats_;
    concord::tasks::BoundedTaskExecutor executor_;
};
// Owner-only local execution settings, without any host-state dependency.
PathGoalCandidateRunner* getPathGoalCandidateRunner(
    bool& checked, std::unique_ptr<PathGoalCandidateRunner>& runner);
