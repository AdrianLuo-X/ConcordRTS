#include "simulation/navigation/pathfinding/PathGoalCandidateTask.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <string_view>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <system_error>

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::uint32_t Present = 1, Passable = 2, Cliff = 4,
    Water = 8, Bridge = 16, Shore = 32;
bool same(PathCell a, PathCell b) { return a.x == b.x && a.y == b.y; }
std::int64_t score(PathCell a, PathCell b) {
    const auto dx = std::abs(static_cast<std::int64_t>(a.x) - b.x);
    const auto dy = std::abs(static_cast<std::int64_t>(a.y) - b.y);
    return std::min(dx, dy) * 14 + (std::max(dx, dy) - std::min(dx, dy)) * 10;
}
bool usable(const PathGoalTaskCell& node, PathMovementMode mode) {
    if (!(node.flags & Present)) return false;
    if (mode == PathMovementMode::Air) return true;
    if (!(node.flags & Passable) || (node.flags & Cliff)) return false;
    if (mode == PathMovementMode::Naval) return (node.flags & (Water | Bridge)) != 0;
    if (mode == PathMovementMode::Ground)
        return !(node.flags & Water) || (node.flags & (Bridge | Shore));
    return true;
}
PathGoalTaskCell sample(PathCell cell, const PathTerrainCell* node) {
    PathGoalTaskCell result{cell, 0};
    if (node) result.flags = Present | (node->passable ? Passable : 0U)
        | (node->cliff ? Cliff : 0U) | (node->water ? Water : 0U)
        | (node->bridge ? Bridge : 0U) | (node->shore ? Shore : 0U);
    return result;
}
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
std::optional<std::string> environmentOption(const char* name) {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    const auto code = _dupenv_s(&value, &length, name);
    const std::unique_ptr<char, decltype(&std::free)> owned(value, &std::free);
    if (code != 0) throw std::runtime_error("Cannot read path candidate execution setting");
    if (value == nullptr) return std::nullopt;
    if (length == 0 || length > 16)
        throw std::invalid_argument("Path candidate execution setting is too long");
    return std::string(value, length - 1);
#else
    const char* value = std::getenv(name);
    if (value == nullptr) return std::nullopt;
    if (std::char_traits<char>::length(value) >= 16)
        throw std::invalid_argument("Path candidate execution setting is too long");
    return std::string(value);
#endif
}
concord::tasks::ExecutorConfig configuration(std::size_t workers,
    concord::tasks::ExecutorConfig limits) {
    if (workers != 0 && workers != 1 && workers != 2 && workers != 4)
        throw std::invalid_argument("Path candidate workers must be 0/1/2/4");
    limits.workers = workers;
    return limits;
}
}

std::optional<PathGoalTaskInput> capturePathGoalTaskInput(
    const PathGoalCandidateInput& request, const PathGoalTerrainView& terrain,
    PathGoalTaskStamp stamp) {
    const auto targets = request.mapQueryApproach
        ? request.approachCells : request.alternateGoalCells;
    if (targets.size() > PathGoalTaskInput::MaxTargets) return std::nullopt;
    PathGoalTaskInput input{};
    input.stamp = stamp;
    input.start = request.start;
    input.goal = request.goal;
    input.movementMode = request.movementMode;
    input.goalSlotCount = request.goalSlotCount;
    input.mapQueryApproach = request.mapQueryApproach;
    input.goalCell = sample(request.goal, terrain.nodeAt(request.goal));
    const auto addSafe = [&request](PathCell cell) {
        return score(cell, request.start) <= std::numeric_limits<int>::max()
            && score(cell, request.goal) <= std::numeric_limits<int>::max();
    };
    for (const auto cell : targets) {
        if (!addSafe(cell)) return std::nullopt;
        input.targets[input.targetCount++] = sample(cell, terrain.nodeAt(cell));
    }
    if (request.goalSlotCount > 1) {
        const auto indices = terrain.indicesInBounds(
            static_cast<std::int64_t>(request.goal.x) - 2,
            static_cast<std::int64_t>(request.goal.y) - 2,
            static_cast<std::int64_t>(request.goal.x) + 2,
            static_cast<std::int64_t>(request.goal.y) + 2);
        if (indices.size() > input.nearby.size()) return std::nullopt;
        for (const auto index : indices) {
            const auto& node = terrain.nodeAtIndex(index);
            if (!addSafe(node.cell)) return std::nullopt;
            input.nearby[input.nearbyCount++] = sample(node.cell, &node);
        }
    }
    return input;
}

PathGoalTaskOutput computePathGoalTask(const PathGoalTaskInput& input,
    std::span<PathCell> scratch) {
    if (input.targetCount > input.targets.size() || input.nearbyCount > input.nearby.size()
        || scratch.size() < PathGoalTaskInput::MaxTargets)
        throw std::invalid_argument("Invalid bounded path candidate input/scratch");
    PathGoalTaskOutput output{};
    output.stamp = input.stamp;
    const auto sort = [&](std::size_t first, bool approach) {
        std::sort(scratch.begin() + static_cast<std::ptrdiff_t>(first),
            scratch.begin() + output.count, [&](PathCell a, PathCell b) {
                const auto firstA = score(a, approach ? input.start : input.goal);
                const auto firstB = score(b, approach ? input.start : input.goal);
                if (firstA != firstB) return firstA < firstB;
                const auto secondA = score(a, approach ? input.goal : input.start);
                const auto secondB = score(b, approach ? input.goal : input.start);
                if (secondA != secondB) return secondA < secondB;
                return a.y != b.y ? a.y < b.y : a.x < b.x;
            });
    };
    for (std::uint32_t i = 0; i < input.targetCount; ++i) {
        const auto& node = input.targets[i];
        if (usable(node, input.movementMode) && !same(node.cell, input.start))
            scratch[output.count++] = node.cell;
    }
    if (output.count) {
        sort(0, input.mapQueryApproach);
        output.mapQueryApproachUsed = input.mapQueryApproach;
    } else if (usable(input.goalCell, input.movementMode)) {
        scratch[output.count++] = input.goal;
        if (input.goalSlotCount > 1) {
            for (std::uint32_t i = 0; i < input.nearbyCount && output.count < 16; ++i) {
                const auto& node = input.nearby[i];
                if (!usable(node, input.movementMode) || same(node.cell, input.start)
                    || same(node.cell, input.goal)) continue;
                const auto dx = std::abs(static_cast<std::int64_t>(node.cell.x) - input.goal.x);
                const auto dy = std::abs(static_cast<std::int64_t>(node.cell.y) - input.goal.y);
                if (std::max(dx, dy) <= 2 && std::max(dx, dy) > 0)
                    scratch[output.count++] = node.cell;
            }
            sort(1, false);
        }
    } else output.requiresFallbackRing = true;
    std::copy_n(scratch.begin(), output.count, output.cells.begin());
    return output;
}

std::size_t pathGoalTaskKernel(const concord::tasks::JobContext& context) {
    static_assert(std::is_trivially_copyable_v<PathGoalTaskInput>);
    static_assert(std::is_trivially_copyable_v<PathGoalTaskOutput>);
    if (context.input.size() != sizeof(PathGoalTaskInput)
        || context.output.size() < sizeof(PathGoalTaskOutput)
        || context.scratch.size() < sizeof(PathCell) * PathGoalTaskInput::MaxTargets)
        throw std::invalid_argument("Invalid path candidate task storage");
    if (context.stopRequested()) return 0;
    const auto started = Clock::now();
    PathGoalTaskInput input;
    std::memcpy(&input, context.input.data(), sizeof(input));
    // Each cell's lifetime begins in its own executor-provided aligned storage.
    auto* scratch = reinterpret_cast<PathCell*>(context.scratch.data());
    for (std::size_t i = 0; i < PathGoalTaskInput::MaxTargets; ++i)
        std::construct_at(scratch + i);
    auto output = computePathGoalTask(input, {scratch, PathGoalTaskInput::MaxTargets});
    output.computeNanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count());
    if (context.stopRequested()) return 0;
    std::memcpy(context.output.data(), &output, sizeof(output));
    return sizeof(output);
}

concord::tasks::Task makePathGoalTask(const PathGoalTaskInput& input) {
    concord::tasks::Task task;
    task.generation = input.stamp.generation;
    task.input = concord::tasks::OwnedBytes(sizeof(input));
    std::memcpy(task.input.bytes().data(), &input, sizeof(input));
    task.outputBytes = sizeof(PathGoalTaskOutput);
    task.scratchBytes = sizeof(PathCell) * PathGoalTaskInput::MaxTargets;
    task.kernel = pathGoalTaskKernel;
    return task;
}

std::optional<PathGoalTaskOutput> takePathGoalTask(
    concord::tasks::BoundedTaskExecutor& executor, concord::tasks::JobKey key,
    const PathGoalTaskStamp& expected) {
    auto taken = executor.waitTake(key);
    if (taken.code != concord::tasks::TakeCode::Taken
        || taken.result.status != concord::tasks::JobStatus::Succeeded
        || taken.result.key != key || taken.result.outputSize != sizeof(PathGoalTaskOutput))
        return std::nullopt;
    PathGoalTaskOutput output;
    std::memcpy(&output, taken.result.output.bytes().data(), sizeof(output));
    if (output.stamp != expected || output.count > output.cells.size()
        || output.searchNodesConsumed != 0) return std::nullopt;
    return output;
}

bool pathGoalTaskEquals(const PathGoalTaskOutput& output, const PathGoalCandidateSet& reference) {
    if (output.count != reference.cells.size() || output.searchNodesConsumed != 0
        || output.mapQueryApproachUsed != reference.mapQueryApproachUsed
        || output.requiresFallbackRing != reference.requiresFallbackRing) return false;
    for (std::size_t i = 0; i < reference.cells.size(); ++i)
        if (!same(output.cells[i], reference.cells[i])) return false;
    return true;
}

PathGoalCandidateSet adoptPathGoalTask(const PathGoalTaskInput& input,
    const std::optional<PathGoalTaskOutput>& completed, bool& usedWorker) {
    usedWorker = completed && completed->stamp == input.stamp
        && completed->count <= completed->cells.size() && completed->searchNodesConsumed == 0;
    std::array<PathCell, PathGoalTaskInput::MaxTargets> scratch{};
    const auto output = usedWorker ? *completed : computePathGoalTask(input, scratch);
    return {{output.cells.begin(), output.cells.begin() + output.count},
        output.mapQueryApproachUsed, output.requiresFallbackRing};
}

PathGoalCandidateRunner::PathGoalCandidateRunner(std::size_t workers, bool report,
    concord::tasks::ExecutorConfig limits, PathGoalCandidateMode mode)
    : workers_(workers), report_(report), mode_(mode), executor_(configuration(workers, limits)) {}

PathGoalCandidateRunner::~PathGoalCandidateRunner() {
    executor_.stopAndJoin();
    if (report_) {
        const auto e = executor_.stats();
        std::cerr << (mode_ == PathGoalCandidateMode::Parallel ? "PathCandidateParallel workers=" : "PathCandidateShadow workers=") << workers_ << " submitted=" << stats_.submitted
            << " compared=" << stats_.compared << " unavailable=" << stats_.unavailable
            << " mismatches=" << stats_.mismatches << " adopted=" << stats_.adopted
            << " synchronousFallbacks=" << stats_.synchronousFallbacks << " snapshotMs=" << stats_.snapshotMs
            << " enqueueMs=" << stats_.enqueueMs << " computeMs=" << stats_.computeMs
            << " waitMs=" << stats_.waitMs << " compareMs=" << stats_.compareMs
            << " peakPayloadBytes=" << e.peakBytes << " fixedStorageBytes=" << e.fixedStorageBytes
            << " producerSnapshotBytes=" << sizeof(PathGoalTaskInput)
            << " ownerResultBytes=" << sizeof(PathGoalTaskOutput) << '\n';
    }
}

PathGoalCandidateSet PathGoalCandidateRunner::generate(const PathGoalCandidateInput& request,
    const PathGoalTerrainView& terrain, PathGoalTaskStamp stamp) {
    stamp.generation = executor_.stats().generation;
    if (sequence_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("Path candidate sequence exhausted");
    stamp.sequence = ++sequence_;
    const auto snapshotStart = Clock::now();
    std::optional<PathGoalTaskInput> input;
    try { input = capturePathGoalTaskInput(request, terrain, stamp); }
    catch (const std::bad_alloc&) {}
    stats_.snapshotMs += elapsed(snapshotStart);
    if (!input) { ++stats_.unavailable; return generatePathGoalCandidates(request, terrain); }
    ++stats_.captured;
    const auto enqueueStart = Clock::now();
    concord::tasks::Submission submitted{concord::tasks::SubmitCode::AllocationFailed, {}};
    try { auto task = makePathGoalTask(*input); submitted = executor_.submit(task); }
    catch (const std::bad_alloc&) {}
    stats_.enqueueMs += elapsed(enqueueStart);
    if (mode_ == PathGoalCandidateMode::Parallel) {
        std::optional<PathGoalTaskOutput> output;
        if (submitted.code == concord::tasks::SubmitCode::Accepted) {
            ++stats_.submitted;
            const auto waitStart = Clock::now();
            output = takePathGoalTask(executor_, submitted.key, stamp);
            stats_.waitMs += elapsed(waitStart);
        }
        bool usedWorker = false;
        auto selected = adoptPathGoalTask(*input, output, usedWorker);
        if (usedWorker) {
            ++stats_.adopted;
            stats_.computeMs += static_cast<double>(output->computeNanoseconds) / 1000000.0;
        } else { ++stats_.unavailable; ++stats_.synchronousFallbacks; }
        return selected;
    }
    // Shadow retains the original synchronous implementation as its oracle.
    auto reference = generatePathGoalCandidates(request, terrain);
    if (submitted.code != concord::tasks::SubmitCode::Accepted) {
        ++stats_.unavailable; return reference;
    }
    ++stats_.submitted;
    const auto waitStart = Clock::now();
    const auto output = takePathGoalTask(executor_, submitted.key, stamp);
    stats_.waitMs += elapsed(waitStart);
    if (!output) { ++stats_.unavailable; return reference; }
    stats_.computeMs += static_cast<double>(output->computeNanoseconds) / 1000000.0;
    const auto compareStart = Clock::now();
    const bool equal = pathGoalTaskEquals(*output, reference);
    stats_.compareMs += elapsed(compareStart);
    ++stats_.compared;
    if (!equal) { ++stats_.mismatches; throw std::runtime_error("Path candidate shadow mismatch"); }
    return reference;
}
PathGoalCandidateRunner* getPathGoalCandidateRunner(bool& checked, std::unique_ptr<PathGoalCandidateRunner>& runner) {
    if (!checked) {
        checked = true;
        const auto mode = environmentOption("CONCORD_PATH_CANDIDATE_MODE");
        if (mode && *mode != "reference") {
            if (*mode != "shadow" && *mode != "parallel")
                throw std::invalid_argument("Path candidate mode must be reference/shadow/parallel");
            const auto value = environmentOption("CONCORD_PATH_CANDIDATE_WORKERS");
            const std::string_view workers = value ? std::string_view(*value) : "1";
            if (workers != "0" && workers != "1" && workers != "2" && workers != "4")
                throw std::invalid_argument("Path candidate workers must be 0/1/2/4");
            try {
                runner = std::make_unique<PathGoalCandidateRunner>(
                    static_cast<std::size_t>(workers.front() - '0'), true, concord::tasks::ExecutorConfig{},
                    *mode == "parallel" ? PathGoalCandidateMode::Parallel : PathGoalCandidateMode::Shadow);
            } catch (const std::bad_alloc&) {
                std::cerr << "PathCandidate allocation failed; synchronous reference retained\n";
            } catch (const std::system_error&) {
                // S34 constructor stops and joins any partially created pool.
                // Local thread resources cannot change the simulation result.
                std::cerr << "PathCandidate worker creation failed; synchronous reference retained\n";
            }
        }
    }
    return runner.get();
}

