#include "simulation/navigation/pathfinding/PathGoalCandidateTask.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace heap_probe {
std::atomic<int> failAfter{-1};
thread_local bool observe = false;
thread_local std::size_t observed = 0;
void* allocate(std::size_t size) {
    if (observe) ++observed;
    if (failAfter.load() >= 0 && failAfter.fetch_sub(1) == 0) throw std::bad_alloc{};
    if (auto* p = std::malloc(std::max(size, std::size_t{1}))) return p;
    throw std::bad_alloc{};
}
}
void* operator new(std::size_t size) { return heap_probe::allocate(size); }
void* operator new[](std::size_t size) { return heap_probe::allocate(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace concord::tasks;
namespace {
std::size_t checks = 0;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
template<class F> void eventually(F f) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!f()) {
        if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("fixture gate timeout");
        std::this_thread::yield();
    }
}
class Terrain final : public PathGoalTerrainView {
public:
    std::vector<PathTerrainCell> cells;
    const PathTerrainCell* nodeAt(PathCell cell) const override {
        for (const auto& n : cells) if (n.cell.x == cell.x && n.cell.y == cell.y) return &n;
        return nullptr;
    }
    const PathTerrainCell& nodeAtIndex(int i) const override { return cells.at(static_cast<std::size_t>(i)); }
    std::vector<int> indicesInBounds(std::int64_t x1, std::int64_t y1,
        std::int64_t x2, std::int64_t y2) const override {
        std::vector<int> result;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            const auto c = cells[i].cell;
            if (c.x >= x1 && c.x <= x2 && c.y >= y1 && c.y <= y2)
                result.push_back(static_cast<int>(i));
        }
        return result;
    }
};
Terrain terrainFor(int scenario) {
    Terrain t;
    for (int y = 0; y < 12; ++y) for (int x = 0; x < 12; ++x) {
        PathTerrainCell n; n.cell = {x, y};
        n.passable = scenario % 3 != 0 || (x + y) % 4 != 0;
        n.water = scenario % 5 == 0 && x > 3;
        n.shore = y == 4; n.bridge = y == 5; n.cliff = scenario % 7 == 0 && x == 6;
        t.cells.push_back(n);
    }
    if (scenario & 1) std::reverse(t.cells.begin(), t.cells.end());
    return t;
}
std::atomic<unsigned> entered{0}, permits{0}, finished{0};
std::array<unsigned, 4> finishOrder{};
std::atomic<unsigned> finishCount{0};
std::atomic<std::size_t> kernelAllocations{0};
std::thread::id inlineThread;
std::size_t observedKernel(const JobContext& c) {
    heap_probe::observed = 0; heap_probe::observe = true;
    const auto n = pathGoalTaskKernel(c);
    heap_probe::observe = false;
    kernelAllocations += heap_probe::observed;
    return n;
}
std::size_t inlineKernel(const JobContext& c) {
    inlineThread = std::this_thread::get_id();
    return observedKernel(c);
}
std::size_t gatedKernel(const JobContext& c) {
    PathGoalTaskInput input;
    std::memcpy(&input, c.input.data(), sizeof(input));
    const auto index = static_cast<unsigned>(input.stamp.sequence);
    entered.fetch_or(1U << index);
    while (!(permits.load() & (1U << index)) && !c.stopRequested()) std::this_thread::yield();
    const auto size = observedKernel(c);
    const auto order = finishCount.fetch_add(1);
    if (order < finishOrder.size()) finishOrder[order] = index;
    finished.fetch_or(1U << index);
    return size;
}
std::size_t failKernel(const JobContext&) { throw std::runtime_error("fixture exception"); }
void resetGates() { entered = 0; permits = 0; finished = 0; finishCount = 0; }

void matrix() {
    static_assert(std::is_trivially_copyable_v<PathGoalTaskInput>);
    static_assert(std::is_trivially_copyable_v<PathGoalTaskOutput>);
    std::size_t cases = 0;
    for (const std::size_t workers : {0U, 1U, 2U, 4U}) {
        BoundedTaskExecutor pool({workers});
        PathGoalCandidateRunner shadow(workers);
        PathGoalCandidateRunner parallel(workers, false, {}, PathGoalCandidateMode::Parallel);
        for (int scenario = 0; scenario < 32; ++scenario) for (int mode = 0; mode < 4; ++mode)
        for (int kind = 0; kind < 4; ++kind) {
            auto terrain = terrainFor(scenario);
            std::vector<PathCell> targets;
            for (int i = 0; i < 64; ++i) targets.push_back({i % 14, (i * 5) % 14});
            PathGoalCandidateInput r;
            r.start = {0, 0}; r.goal = scenario % 11 == 0 ? PathCell{20, 20} : PathCell{6, 6};
            r.movementMode = static_cast<PathMovementMode>(mode);
            r.goalSlotCount = kind == 1 ? 4 : 0;
            r.mapQueryApproach = kind == 2;
            if (kind == 2) r.approachCells = targets;
            if (kind == 3) r.alternateGoalCells = targets;
            const auto reference = generatePathGoalCandidates(r, terrain);
            const PathGoalTaskStamp stamp{1, ++cases, 123, 456, 789, 1, static_cast<int>(cases), 37};
            auto captured = capturePathGoalTaskInput(r, terrain, stamp);
            require(captured.has_value(), "matrix bounded capture");
            auto task = makePathGoalTask(*captured); task.kernel = observedKernel;
            const auto accepted = pool.submit(task);
            require(accepted.code == SubmitCode::Accepted, "matrix admission");
            const auto output = takePathGoalTask(pool, accepted.key, stamp);
            require(output && pathGoalTaskEquals(*output, reference), "complete candidate oracle");
            require(!takePathGoalTask(pool, accepted.key, stamp), "single result claim");
            const auto formal = parallel.generate(r, terrain, stamp);
            require(formal.cells.size() == reference.cells.size()
                && formal.mapQueryApproachUsed == reference.mapQueryApproachUsed
                && formal.requiresFallbackRing == reference.requiresFallbackRing, "formal complete flags");
            for (std::size_t i = 0; i < reference.cells.size(); ++i)
                require(formal.cells[i].x == reference.cells[i].x && formal.cells[i].y == reference.cells[i].y,
                    "formal complete original order");
            const auto selected = shadow.generate(r, terrain, stamp);
            require(selected.cells.size() == reference.cells.size()
                && selected.mapQueryApproachUsed == reference.mapQueryApproachUsed
                && selected.requiresFallbackRing == reference.requiresFallbackRing, "shadow reference flags");
            for (std::size_t i = 0; i < reference.cells.size(); ++i)
                require(selected.cells[i].x == reference.cells[i].x && selected.cells[i].y == reference.cells[i].y,
                    "shadow original ordered cells");
        }
            // Formal consumer uses the worker output, with the same complete
            // ordered set already checked against the unchanged S22 oracle.
        require(parallel.stats().adopted == 512 && parallel.stats().synchronousFallbacks == 0,
            "matrix all formal results adopted");
        require(shadow.stats().mismatches == 0 && shadow.stats().compared == 512, "matrix all shadow compared");
        require(pool.stats().peakBytes <= 16U * 1024U * 1024U, "payload budget");
    }
    require(kernelAllocations == 0, "pure production kernel performs no ordinary C++ heap allocation");
    std::cout << "matrix cases=" << cases << " fullOracle=yes workers=0,1,2,4 kernelAllocations=0\n";
}
void requireFallback(const PathGoalTaskInput& input, const std::optional<PathGoalTaskOutput>& result) {
    bool used = true;
    const auto selected = adoptPathGoalTask(input, result, used);
    std::array<PathCell, 64> scratch{};
    require(!used && pathGoalTaskEquals(computePathGoalTask(input, scratch), selected),
        "actual rejected claim uses complete same-input production fallback");
}
void adversarial() {
    auto terrain = terrainFor(2);
    PathGoalCandidateInput r; r.start = {0, 0}; r.goal = {6, 6}; r.goalSlotCount = 4;
    const auto reference = generatePathGoalCandidates(r, terrain);
    auto input = *capturePathGoalTaskInput(r, terrain, {1, 0, 5, 6, 7, 8, 9, 10});
    const auto ownerThread = std::this_thread::get_id();
    {
        BoundedTaskExecutor pool({0});
        auto task = makePathGoalTask(input); task.kernel = inlineKernel;
        const auto sub = pool.submit(task);
        require(inlineThread == ownerThread, "zero workers executes in submit caller");
        require(takePathGoalTask(pool, sub.key, input.stamp).has_value(), "inline complete before return");
        require(pool.stats().workerThreads == 0, "zero actual worker threads");
    }
    {
        BoundedTaskExecutor pool({4}); resetGates();
        std::array<JobKey, 4> keys{};
        std::array<PathGoalTaskStamp, 4> stamps{};
        for (unsigned i = 0; i < 4; ++i) {
            auto owned = input; owned.stamp.sequence = i; stamps[i] = owned.stamp;
            auto task = makePathGoalTask(owned); task.kernel = gatedKernel;
            auto sub = pool.submit(task); require(sub.code == SubmitCode::Accepted, "reverse admitted"); keys[i] = sub.key;
        }
        eventually([] { return entered == 15; });
        // Original views, spans and terrain can die before these kernels begin.
        terrain.cells.clear(); terrain.cells.shrink_to_fit();
        for (int i = 3; i >= 0; --i) {
            permits.fetch_or(1U << i);
            eventually([i] { return (finished.load() & (1U << i)) != 0; });
        }
        for (unsigned i = 0; i < 4; ++i) {
            auto result = takePathGoalTask(pool, keys[i], stamps[i]);
            require(result && pathGoalTaskEquals(*result, reference), "owned input survives reverse completion");
            auto current = input; current.stamp = stamps[i]; bool used = false;
            const auto selected = adoptPathGoalTask(current, result, used);
            require(used && pathGoalTaskEquals(*result, selected), "formal consumer adopts strict reversed result");
            requireFallback(current, takePathGoalTask(pool, keys[i], stamps[i]));
            require(finishOrder[i] == 3 - i, "strict reversed actual kernel completion");
        }
        require(pool.stats().peakRunning == 4, "actual four workers executing");
    }
    for (const std::size_t workers : {1U, 2U, 4U}) {
        BoundedTaskExecutor pool({workers}); resetGates();
        auto task = makePathGoalTask(input); task.kernel = gatedKernel;
        auto sub = pool.submit(task); eventually([] { return entered == 1; });
        require(pool.cancel(sub.key), "running cancellation");
        const auto rejected = takePathGoalTask(pool, sub.key, input.stamp); require(!rejected, "cancel never publishes partial output"); requireFallback(input, rejected);
        pool.stopAndJoin();
    }
    {
        BoundedTaskExecutor pool({1}); resetGates();
        auto task = makePathGoalTask(input); task.kernel = gatedKernel;
        const auto sub = pool.submit(task); eventually([] { return entered == 1; });
        require(pool.advanceGeneration(2), "generation advance");
        const auto rejected = takePathGoalTask(pool, sub.key, input.stamp); require(!rejected, "stale generation rejected"); requireFallback(input, rejected);
        permits = 1; pool.stopAndJoin();
    }
    for (int field = 0; field < 8; ++field) {
        BoundedTaskExecutor pool({0}); auto task = makePathGoalTask(input);
        const auto sub = pool.submit(task); auto expected = input.stamp;
        switch (field) {
        case 0: ++expected.mapRevision; break; case 1: ++expected.occupancyRevision; break;
        case 2: ++expected.simulationTick; break; case 3: ++expected.requestId; break;
        case 4: ++expected.queueTick; break; case 5: ++expected.searchBudget; break;
        case 6: ++expected.sequence; break; default: ++expected.generation; break;
        }
        require(!takePathGoalTask(pool, sub.key, expected), "all identity/revision/budget fields validated");
    }
    {
        BoundedTaskExecutor pool({1}); auto task = makePathGoalTask(input); task.kernel = failKernel;
        auto sub = pool.submit(task); const auto rejected = takePathGoalTask(pool, sub.key, input.stamp); require(!rejected, "worker exception rejected"); requireFallback(input, rejected);
        task = makePathGoalTask(input); sub = pool.submit(task);
        require(takePathGoalTask(pool, sub.key, input.stamp).has_value(), "worker survives exception");
    }
    {
        auto task = makePathGoalTask(input); const auto bytes = task.input.size();
        ExecutorConfig config; config.maxInFlightBytes = bytes;
        BoundedTaskExecutor pool(config);
        require(pool.submit(task).code == SubmitCode::MemoryFull && task.input.size() == bytes,
            "byte rejection retains caller input");
        heap_probe::failAfter = 0; bool failed = false;
        try { auto allocated = makePathGoalTask(input); (void)allocated; }
        catch (const std::bad_alloc&) { failed = true; }
        heap_probe::failAfter = -1; require(failed, "snapshot byte allocation failure observed");
        BoundedTaskExecutor normal;
        heap_probe::failAfter = 0;
        const auto sub = normal.submit(task);
        heap_probe::failAfter = -1;
        require(sub.code == SubmitCode::AllocationFailed && task.input.size() == bytes,
            "executor output allocation failure retains input");
    }
    {
        ExecutorConfig config; config.workers = 1; config.queueCapacity = 1; config.maxInFlightJobs = 3;
        BoundedTaskExecutor pool(config); resetGates();
        auto task = makePathGoalTask(input); task.kernel = gatedKernel;
        const auto running = pool.submit(task); eventually([] { return entered == 1; });
        task = makePathGoalTask(input); const auto queued = pool.submit(task);
        task = makePathGoalTask(input); require(pool.submit(task).code == SubmitCode::QueueFull, "queue full rejection");
        require(pool.cancel(queued.key), "queued cancel");
        const auto rejected = takePathGoalTask(pool, queued.key, input.stamp); require(!rejected, "queued cancel not published"); requireFallback(input, rejected);
        permits = 1; require(takePathGoalTask(pool, running.key, input.stamp).has_value(), "delayed result completes");
    }
    terrain = terrainFor(2);
    {
        ExecutorConfig limits; limits.maxInFlightBytes = 1;
        PathGoalCandidateRunner runner(1, false, limits);
        const auto result = runner.generate(r, terrain, input.stamp);
        PathGoalTaskOutput expected{}; std::array<PathCell, 64> scratch{};
        expected = computePathGoalTask(input, scratch);
        require(pathGoalTaskEquals(expected, result), "shadow resource failure keeps complete reference");
        require(runner.stats().unavailable == 1 && runner.stats().submitted == 0, "rejection observed");
    }
    {
        ExecutorConfig limits; limits.maxInFlightBytes = 1;
        PathGoalCandidateRunner runner(4, false, limits, PathGoalCandidateMode::Parallel);
        const auto selected = runner.generate(r, terrain, input.stamp);
        std::array<PathCell, 64> scratch{};
        require(pathGoalTaskEquals(computePathGoalTask(input, scratch), selected), "formal admission failure same input fallback");
        require(runner.stats().adopted == 0 && runner.stats().synchronousFallbacks == 1,
            "formal shortage does not omit work");
    }
    // All unsuccessful claims (cancel, exception, duplicate, stale identity)
    // enter this exact production consumer, never partially shared output.
    for (int reason = 0; reason < 12; ++reason) {
        std::optional<PathGoalTaskOutput> completed;
        std::array<PathCell, 64> scratch{};
        if (reason < 10) {
            completed = computePathGoalTask(input, scratch);
            switch (reason) {
            case 0: ++completed->stamp.generation; break; case 1: ++completed->stamp.sequence; break;
            case 2: ++completed->stamp.mapRevision; break; case 3: ++completed->stamp.occupancyRevision; break;
            case 4: ++completed->stamp.simulationTick; break; case 5: ++completed->stamp.queueTick; break;
            case 6: ++completed->stamp.requestId; break; case 7: ++completed->stamp.searchBudget; break;
            case 8: completed->count = 65; break; default: completed->searchNodesConsumed = 1; break;
            }
        }
        bool used = true;
        const auto selected = adoptPathGoalTask(input, completed, used);
        require(!used && pathGoalTaskEquals(computePathGoalTask(input, scratch), selected),
            "invalid or absent formal result recomputes identical complete input");
    }
    std::vector<PathCell> tooMany(65, {1, 1}); r.approachCells = tooMany; r.mapQueryApproach = true;
    require(!capturePathGoalTaskInput(r, terrain, input.stamp), "oversized snapshot rejected without weakening cap");
    std::cout << "adversarial reverse=3,2,1,0 cancellation=running,queued stale=all8fields duplicate=rejected exception=recovered resources=queue,bytes,allocation zero=synchronous lifetime=owned\n";
}
}
int main() {
    try {
        matrix(); adversarial();
        std::cout << "Path goal candidate task contract passed checks=" << checks
            << " inputBytes=" << sizeof(PathGoalTaskInput) << " outputBytes=" << sizeof(PathGoalTaskOutput)
            << " scratchBytes=" << 64 * sizeof(PathCell) << '\n';
        return 0;
    } catch (const std::exception& e) {
        heap_probe::failAfter = -1;
        std::cerr << "Path goal candidate task contract failed: " << e.what() << " checks=" << checks << '\n';
        return 1;
    }
}