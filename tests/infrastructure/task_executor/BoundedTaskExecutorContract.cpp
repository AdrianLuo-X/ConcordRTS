#include "core/tasks/BoundedTaskExecutor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>
#include <type_traits>

// Test-only ordinary C++ heap observer and deterministic allocation failure.
// No product hooks, native handles or test branches in the executor.
namespace heap_probe {
struct alignas(std::max_align_t) Header { std::size_t size; };
std::atomic<std::size_t> live{0}, bytes{0};
std::atomic<int> failAfter{-1};
void* allocate(std::size_t size) {
    const int countdown = failAfter.load();
    if (countdown >= 0 && failAfter.fetch_sub(1) == 0) throw std::bad_alloc{};
    if (size > std::numeric_limits<std::size_t>::max() - sizeof(Header)) throw std::bad_alloc{};
    auto* block = static_cast<Header*>(std::malloc(sizeof(Header) + std::max(size, std::size_t{1})));
    if (block == nullptr) throw std::bad_alloc{};
    block->size = size;
    ++live;
    bytes += size;
    return block + 1;
}
void release(void* pointer) noexcept {
    if (pointer == nullptr) return;
    auto* block = static_cast<Header*>(pointer) - 1;
    bytes -= block->size;
    --live;
    std::free(block);
}
}
void* operator new(std::size_t size) { return heap_probe::allocate(size); }
void* operator new[](std::size_t size) { return heap_probe::allocate(size); }
void operator delete(void* pointer) noexcept { heap_probe::release(pointer); }
void operator delete[](void* pointer) noexcept { heap_probe::release(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { heap_probe::release(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { heap_probe::release(pointer); }

using namespace concord::tasks;
using namespace std::chrono_literals;
namespace {
std::atomic<std::size_t> checks{0};
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class Predicate> void eventually(Predicate predicate, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error(message);
        std::this_thread::yield();
    }
    ++checks;
}
// Only fixture kernels use these gates/observations. Production kernels have no
// external state; gates force completion order without timing-dependent sleeps.
std::atomic<unsigned> entered{0}, exited{0}, permits{0}, observed{0};
std::thread::id callbackThread;
BoundedTaskExecutor* ownPool = nullptr;
std::size_t transform(const JobContext& context) {
    for (std::size_t i = 0; i < context.input.size(); ++i) {
        context.scratch[i] = context.input[i] ^ std::byte{0x5a};
        context.output[i] = context.scratch[i];
    }
    return context.input.size();
}
std::size_t inlineObservation(const JobContext& context) {
    callbackThread = std::this_thread::get_id();
    return transform(context);
}
std::size_t gate(const JobContext& context) {
    const auto index = std::to_integer<unsigned>(context.input[0]);
    entered.fetch_or(1U << index);
    while ((permits.load() & (1U << index)) == 0 && !context.stopRequested())
        std::this_thread::yield();
    exited.fetch_or(1U << index);
    context.output[0] = context.input[0];
    return 1;
}
std::size_t failStandard(const JobContext&) { throw std::runtime_error("fixture worker exception"); }
std::size_t failUnknown(const JobContext&) { throw 73; }
std::size_t overflow(const JobContext& context) { return context.output.size() + 1; }
std::size_t holdCancelledStorage(const JobContext& context) {
    entered = 1;
    while (!context.stopRequested()) std::this_thread::yield();
    observed = 1;
    while (permits.load() == 0) std::this_thread::yield();
    // Observe the same owned input/scratch after the owner invalidates it.
    context.scratch[0] = context.input[0];
    context.output[0] = context.scratch[0];
    exited = 1;
    return 1;
}
std::size_t selfGuard(const JobContext& context) {
    unsigned flags = 0;
    if (ownPool->waitTake(context.key).code == TakeCode::WouldDeadlock) flags |= 1;
    if (!ownPool->stopAndJoin()) flags |= 2;
    Task nested;
    nested.kernel = transform;
    if (ownPool->submit(nested).code == SubmitCode::Reentrant) flags |= 4;
    observed.store(flags);
    return 0;
}
Task task(unsigned index = 0, JobKernel kernel = transform, std::size_t bytes = 8) {
    Task value;
    value.input = OwnedBytes(bytes);
    value.outputBytes = bytes;
    value.scratchBytes = bytes;
    value.kernel = kernel;
    for (std::size_t i = 0; i < bytes; ++i) value.input.bytes()[i] = std::byte((index + i) % 256);
    return value;
}
JobKey admit(BoundedTaskExecutor& executor, Task& value) {
    const auto result = executor.submit(value);
    require(result.code == SubmitCode::Accepted, "expected admission");
    require(value.input.size() == 0 && value.input.bytes().empty(), "input ownership transferred");
    return result.key;
}
void resetGates() { entered = 0; exited = 0; permits = 0; }
void drained(BoundedTaskExecutor& pool) {
    const auto state = pool.stats();
    require(state.inFlightJobs == 0 && state.reservedBytes == 0 && state.queued == 0 && state.running == 0,
        "all payload reservations returned");
}

void modes() {
    for (const std::size_t workers : {0U, 1U, 2U, 4U}) {
        BoundedTaskExecutor pool({workers, 16, 16, 4096});
        JobKey keys[16];
        const auto ownerThread = std::this_thread::get_id();
        for (unsigned i = 0; i < 16; ++i) {
            auto value = task(i, workers == 0 ? inlineObservation : transform);
            keys[i] = admit(pool, value);
            require(keys[i].id == i + 1 && keys[i].generation == 1, "stable job identity");
            if (workers == 0) require(callbackThread == ownerThread, "zero mode ran on caller thread");
        }
        if (workers != 0) eventually([&] { return pool.stats().workerThreads == workers; }, "workers started");
        else require(pool.stats().workerThreads == 0, "zero mode has no worker");
        auto full = task();
        require(pool.submit(full).code == SubmitCode::InFlightFull, "unclaimed results bounded by slots");
        require(full.input.size() == 8, "rejected input remains caller owned");
        for (unsigned i = 0; i < 16; ++i) {
            auto taken = pool.waitTake(keys[i]);
            require(taken.code == TakeCode::Taken && taken.result.status == JobStatus::Succeeded, "mode success");
            require(taken.result.key == keys[i] && taken.result.outputSize == 8, "result identity and size");
            for (unsigned j = 0; j < 8; ++j)
                require(taken.result.output.bytes()[j] == (std::byte(i + j) ^ std::byte{0x5a}), "identical mode bytes");
            require(pool.tryTake(keys[i]).code == TakeCode::UnknownJob, "duplicate result rejected");
        }
        drained(pool);
        const auto stats = pool.stats();
        require(stats.peakJobs == 16 && stats.peakBytes == 16 * 24, "observed reservation peaks");
        require(stats.peakRunning <= std::max(workers, std::size_t{1}), "execution concurrency bounded");
        require(pool.stopAndJoin() && pool.stopAndJoin(), "idempotent stop and join");
        require(pool.stats().workerThreads == 0, "all workers joined");
        std::cout << "mode workers=" << workers << " jobs=16 peakBytes=" << stats.peakBytes << '\n';
    }
}
void reverseCompletion() {
    resetGates();
    BoundedTaskExecutor pool({4, 4, 4, 64});
    JobKey keys[4];
    for (unsigned i = 0; i < 4; ++i) { auto value = task(i, gate, 1); keys[i] = admit(pool, value); }
    eventually([] { return entered.load() == 15; }, "four jobs simultaneously blocked");
    for (unsigned reverse = 4; reverse > 0; --reverse) {
        const unsigned i = reverse - 1;
        require(pool.tryTake(keys[i]).code == TakeCode::NotReady, "gate truly blocks completion");
        permits.fetch_or(1U << i);
        auto result = pool.waitTake(keys[i]);
        require(result.code == TakeCode::Taken && result.result.output.bytes()[0] == std::byte(i), "reverse result");
        require(exited.load() == (15U & ~((1U << i) - 1U)), "forced exact reverse completion");
    }
    drained(pool);
}
void boundsAndFailure() {
    resetGates();
    BoundedTaskExecutor pool({1, 1, 3, 64});
    auto running = task(0, gate, 1); const auto a = admit(pool, running);
    eventually([] { return entered.load() == 1; }, "worker occupied");
    auto queued = task(1, gate, 1); const auto b = admit(pool, queued);
    auto rejected = task(2, gate, 1);
    require(pool.submit(rejected).code == SubmitCode::QueueFull, "queue full is nonblocking rejection");
    require(rejected.input.size() == 1, "full queue preserves task");
    require(pool.cancel(b), "cancel queued job");
    require(pool.waitTake(b).result.status == JobStatus::Cancelled, "queued cancellation terminal");
    const auto c = admit(pool, rejected);
    require(c.id > b.id, "IDs not reused after cancellation");
    require(pool.cancel(a), "cancel running job");
    require(pool.waitTake(a).result.status == JobStatus::Cancelled, "running cancellation terminal");
    pool.requestStop();
    require(pool.waitTake(c).result.status == JobStatus::Cancelled, "close cancels pending/running");
    require(pool.stopAndJoin(), "join after close");
    require((entered.load() & 2U) == 0, "cancelled queued kernel never called");
    require(pool.submit(rejected).code == SubmitCode::Stopped, "stopped rejects admission");
    drained(pool);

    BoundedTaskExecutor stopped({0, 1, 1, 24});
    Task empty;
    require(stopped.submit(empty).code == SubmitCode::InvalidTask, "null kernel rejected");
    empty.kernel = transform;
    const auto emptyKey = admit(stopped, empty);
    stopped.requestStop();
    const auto stoppedResult = stopped.waitTake(emptyKey);
    require(stoppedResult.result.status == JobStatus::Cancelled && stoppedResult.result.output.size() == 0,
        "close also suppresses already completed unclaimed output");
    require(!stopped.cancel(emptyKey) && !stopped.advanceGeneration(2), "closed/taken job cannot reopen");
    drained(stopped);

    BoundedTaskExecutor budget({0, 2, 2, 24});
    auto first = task(); const auto firstKey = admit(budget, first);
    auto extra = task(0, transform, 1);
    require(budget.submit(extra).code == SubmitCode::MemoryFull, "completed output still occupies byte budget");
    require(budget.stats().reservedBytes == 24, "exact input output scratch reservation");
    require(budget.waitTake(firstKey).result.status == JobStatus::Succeeded, "take releases budget");
    extra.outputBytes = std::numeric_limits<std::size_t>::max();
    require(budget.submit(extra).code == SubmitCode::MemoryFull, "overflow cannot bypass budget");
    extra.outputBytes = 1;
    heap_probe::failAfter = 0;
    const auto allocation = budget.submit(extra);
    heap_probe::failAfter = -1;
    require(allocation.code == SubmitCode::AllocationFailed && extra.input.size() == 1, "allocation failure preserves input");
    drained(budget);
    heap_probe::failAfter = 1;
    const auto scratchAllocation = budget.submit(extra);
    heap_probe::failAfter = -1;
    require(scratchAllocation.code == SubmitCode::AllocationFailed && extra.input.size() == 1,
        "scratch allocation failure releases already allocated output");
    drained(budget);
    const auto afterFailure = admit(budget, extra);
    require(afterFailure.id == 2, "failed admission does not consume ID");
    require(budget.waitTake(afterFailure).result.status == JobStatus::Succeeded, "pool survives allocation failure");
    drained(budget);
    for (const auto workers : {0U, 1U, 2U, 4U}) {
        BoundedTaskExecutor failures({workers, 4, 4, 128});
        for (const auto kernel : {failStandard, failUnknown, overflow}) {
            auto value = task(0, kernel); const auto key = admit(failures, value);
            auto result = failures.waitTake(key);
            require(result.result.status == JobStatus::Failed && result.result.error[0] != '\0', "bounded exception/overflow result");
            require(result.result.output.size() == 0 && result.result.outputSize == 0, "failure never publishes partial output");
        }
        auto next = task(); const auto key = admit(failures, next);
        require(failures.waitTake(key).result.status == JobStatus::Succeeded, "worker survives kernel exceptions");
        drained(failures);
    }
}
void generations() {
    resetGates();
    BoundedTaskExecutor pool({1, 2, 4, 96});
    auto completed = task(); const auto oldComplete = admit(pool, completed);
    eventually([&] { return pool.stats().running == 0 && pool.stats().queued == 0; }, "old result completed");
    auto running = task(0, gate, 1); const auto oldRunning = admit(pool, running);
    eventually([] { return entered.load() == 1; }, "old generation executing");
    auto queued = task(1, gate, 1); const auto oldQueued = admit(pool, queued);
    require(pool.advanceGeneration(2), "advance generation");
    for (const auto key : {oldComplete, oldRunning, oldQueued}) {
        require(pool.waitTake(key).code == TakeCode::StaleGeneration, "stale generation rejected");
        require(!pool.cancel(key), "stale cancellation rejected");
    }
    eventually([&] { return pool.stats().inFlightJobs == 0; }, "old running storage reclaimed after return");
    require((entered.load() & 2U) == 0, "stale queued kernel never executed");
    auto fresh = task();
    require(pool.submit(fresh).code == SubmitCode::StaleGeneration, "stale submission rejected");
    fresh.generation = 2; const auto key = admit(pool, fresh);
    require(key.id > oldQueued.id && key.generation == 2, "ID monotonic across generations");
    require(pool.waitTake(key).result.status == JobStatus::Succeeded, "new generation succeeds");
    require(pool.tryTake(key).code == TakeCode::UnknownJob, "new generation duplicate refused");
    require(!pool.advanceGeneration(2) && !pool.advanceGeneration(1) && !pool.advanceGeneration(0), "generation cannot repeat or rewind");
    drained(pool);

    resetGates(); observed = 0;
    BoundedTaskExecutor held({1, 1, 1, 3});
    auto storage = task(7, holdCancelledStorage, 1);
    const auto heldKey = admit(held, storage);
    eventually([] { return entered.load() == 1; }, "owned storage kernel entered");
    require(held.advanceGeneration(2), "invalidate running storage");
    eventually([] { return observed.load() == 1; }, "kernel observes generation cancellation");
    require(held.stats().reservedBytes == 3 && held.stats().inFlightJobs == 1,
        "old generation storage remains reserved until kernel exits");
    require(held.tryTake(heldKey).code == TakeCode::StaleGeneration, "held old result not published");
    auto blocked = task(0, transform, 1); blocked.generation = 2;
    require(held.submit(blocked).code == SubmitCode::InFlightFull, "generation cannot bypass running resource cap");
    permits = 1;
    eventually([&] { return held.stats().inFlightJobs == 0; }, "old generation reclaimed after kernel exit");
    const auto freshKey = admit(held, blocked);
    require(held.waitTake(freshKey).result.status == JobStatus::Succeeded, "reuse only after old kernel exit");
    drained(held);
}
void competingResultTakers() {
    resetGates();
    BoundedTaskExecutor pool({1, 1, 1, 3});
    auto value = task(0, gate, 1); const auto key = admit(pool, value);
    eventually([] { return entered.load() == 1; }, "result race gate entered");
    std::atomic<unsigned> ready{0}, taken{0}, unknown{0};
    auto take = [&] {
        ++ready;
        auto result = pool.waitTake(key);
        if (result.code == TakeCode::Taken) ++taken;
        if (result.code == TakeCode::UnknownJob) ++unknown;
    };
    std::thread first(take), second(take);
    eventually([&] { return ready.load() == 2; }, "both result takers started");
    permits = 1;
    first.join(); second.join();
    require(taken == 1 && unknown == 1, "exactly one owner can acquire output");
    drained(pool);
}
void shutdownAndReentrancy() {
    for (const auto workers : {0U, 1U, 2U, 4U}) {
        BoundedTaskExecutor pool({workers, 2, 2, 64});
        ownPool = &pool; observed = 0;
        auto value = task(0, selfGuard); const auto key = admit(pool, value);
        require(pool.waitTake(key).result.status == JobStatus::Succeeded && observed.load() == 7,
            "same pool wait/join/recursive submission rejected");
        ownPool = nullptr;
    }
    for (const auto workers : {0U, 1U, 2U, 4U}) {
        resetGates();
        // Owner resource declared first; pool destructor must finish while this
        // resource is alive. No resource address is passed to the kernel.
        struct Resource {
            ~Resource() { require(exited.load() == 1, "kernel exited before owner resource destruction"); }
        } resource;
        auto pool = std::make_unique<BoundedTaskExecutor>(ExecutorConfig{workers, 2, 2, 64});
        auto value = task(0, gate, 1);
        if (workers == 0) {
            auto* executor = pool.get();
            std::thread caller([&] { require(executor->submit(value).code == SubmitCode::Accepted, "concurrent inline caller"); });
            eventually([] { return entered.load() == 1; }, "inline job entered");
            require(pool->stopAndJoin(), "join waits for inline invocation too");
            caller.join();
            pool.reset();
        } else {
            admit(*pool, value);
            eventually([] { return entered.load() == 1; }, "destructor has live job");
            pool.reset();
        }
        require(exited.load() == 1, "destructor cancelled and joined");
    }
}
void invalidConfigAndHeap() {
    for (const auto config : {ExecutorConfig{33, 1, 1, 1}, ExecutorConfig{0, 0, 1, 1},
            ExecutorConfig{0, 2, 1, 1}, ExecutorConfig{0, 1, 4097, 1},
            ExecutorConfig{0, 1, 1, 0}, ExecutorConfig{0, 1, 1, 1, 0}}) {
        bool rejected = false;
        try { BoundedTaskExecutor invalid(config); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "invalid config rejected before starting workers");
    }
    // Thread/STL initialization has already run above. Observe every ordinary
    // C++ allocation for repeated construction, cancellation, failure and join.
    const auto beforeCount = heap_probe::live.load();
    const auto beforeBytes = heap_probe::bytes.load();
    for (unsigned repeat = 0; repeat < 40; ++repeat) {
        BoundedTaskExecutor pool({repeat % 3 + 1, 4, 4, 128});
        auto one = task(0, failUnknown); auto two = task();
        const auto a = admit(pool, one); const auto b = admit(pool, two);
        pool.cancel(b);
        require(pool.waitTake(a).result.status == JobStatus::Failed, "repeat failure retained");
        require(pool.waitTake(b).result.status == JobStatus::Cancelled, "repeat cancelled output discarded");
        drained(pool);
    }
    require(heap_probe::live.load() == beforeCount && heap_probe::bytes.load() == beforeBytes,
        "no ordinary C++ heap allocation leaked across forty pool lifetimes");
    // Exercise constructor unwind at each allocation/start position. This also
    // injects failure after one or more workers have actually been constructed.
    unsigned failed = 0, succeeded = 0;
    for (int fail = 0; fail < 12; ++fail) {
        bool threw = false;
        heap_probe::failAfter = fail;
        try { BoundedTaskExecutor pool({4, 4, 4, 128}); }
        catch (const std::bad_alloc&) { threw = true; }
        heap_probe::failAfter = -1;
        if (threw) ++failed; else ++succeeded;
        require(heap_probe::live.load() == beforeCount && heap_probe::bytes.load() == beforeBytes,
            "constructor failure joined workers and released allocations");
    }
    require(failed > 4 && succeeded != 0, "startup allocation failure matrix exercised");
    std::cout << "heap lifetimes=40 startupFailed=" << failed << " startupSucceeded=" << succeeded << '\n';
}
}

int main() {
    try {
        static_assert(!std::is_copy_constructible_v<OwnedBytes>);
        static_assert(!std::is_move_constructible_v<BoundedTaskExecutor>);
        std::cout << "S34 bounded executor contract\n";
        modes();
        reverseCompletion();
        boundsAndFailure();
        generations();
        competingResultTakers();
        shutdownAndReentrancy();
        invalidConfigAndHeap();
        std::cout << "passed checks=" << checks << " workers=0,1,2,4 realConsumer=false\n";
        return 0;
    } catch (const std::exception& error) {
        heap_probe::failAfter = -1;
        std::cerr << "FAILED after checks=" << checks << ": " << error.what() << '\n';
        return 1;
    }
}
