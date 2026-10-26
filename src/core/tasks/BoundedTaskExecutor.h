#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace concord::tasks {

// Exact-size storage. No adoption of borrowed pointers, vector spare capacity,
// shared writable caches or live World objects is part of this interface.
class OwnedBytes {
public:
    OwnedBytes() noexcept = default;
    explicit OwnedBytes(std::size_t size);
    OwnedBytes(OwnedBytes&& other) noexcept;
    OwnedBytes& operator=(OwnedBytes&& other) noexcept;
    OwnedBytes(const OwnedBytes&) = delete;
    OwnedBytes& operator=(const OwnedBytes&) = delete;
    std::span<std::byte> bytes() noexcept;
    std::span<const std::byte> bytes() const noexcept;
    std::size_t size() const noexcept { return size_; }
private:
    std::unique_ptr<std::byte[]> data_;
    std::size_t size_ = 0;
};

struct JobKey {
    std::uint64_t id = 0; // Monotonic within one executor; never recycled.
    std::uint64_t generation = 0;
    bool operator==(const JobKey&) const = default;
};

class JobContext {
public:
    const JobKey key;
    const std::span<const std::byte> input;
    const std::span<std::byte> output;
    const std::span<std::byte> scratch;
    bool stopRequested() const noexcept;
private:
    friend class BoundedTaskExecutor;
    JobContext(JobKey key, std::span<const std::byte> input,
        std::span<std::byte> output, std::span<std::byte> scratch,
        const std::atomic<bool>& cancelled) noexcept;
    const std::atomic<bool>& cancelled_;
};

// Return the used output length. Kernels use only these spans and bounded stack
// locals, poll cancellation, and never retain the context. No external mutable
// state, dynamic allocation, executor ownership or blocking pool dependencies.
// In particular, a capturing lambda/std::function cannot be submitted.
using JobKernel = std::size_t (*)(const JobContext&);
struct Task {
    std::uint64_t generation = 1;
    OwnedBytes input;
    std::size_t outputBytes = 0;
    std::size_t scratchBytes = 0;
    JobKernel kernel = nullptr;
};

struct ExecutorConfig {
    std::size_t workers = 0; // 0 executes inline before submit returns.
    std::size_t queueCapacity = 16;
    std::size_t maxInFlightJobs = 20; // Includes unclaimed terminal results.
    std::size_t maxInFlightBytes = 16U * 1024U * 1024U;
    std::uint64_t initialGeneration = 1;
};

enum class SubmitCode {
    Accepted, QueueFull, InFlightFull, MemoryFull, InvalidTask,
    StaleGeneration, Stopped, Reentrant, AllocationFailed, IdExhausted
};
struct Submission { SubmitCode code; JobKey key{}; };
enum class JobStatus { Succeeded, Cancelled, Failed };
struct TaskResult {
    JobKey key{};
    JobStatus status = JobStatus::Cancelled;
    OwnedBytes output; // Present only on success; becomes caller-owned on take.
    std::size_t outputSize = 0;
    std::array<char, 160> error{}; // Bounded copy; no retained exception_ptr.
};
enum class TakeCode { Taken, NotReady, UnknownJob, StaleGeneration, WouldDeadlock };
struct TakenResult { TakeCode code; TaskResult result{}; };
struct ExecutorStats {
    std::size_t workerThreads = 0;
    std::size_t queued = 0;
    std::size_t running = 0;
    std::size_t inFlightJobs = 0;
    std::size_t reservedBytes = 0;
    std::size_t peakJobs = 0;
    std::size_t peakRunning = 0;
    std::size_t peakBytes = 0;
    std::size_t fixedStorageBytes = 0; // Impl + slot/ring/thread arrays, not CRT stacks.
    std::uint64_t generation = 0;
    bool stopped = false;
};

class BoundedTaskExecutor {
public:
    static constexpr std::size_t MaxWorkers = 32;
    static constexpr std::size_t MaxJobs = 4096;
    static constexpr std::size_t MaxPayloadBytes = 1024U * 1024U * 1024U;
    explicit BoundedTaskExecutor(ExecutorConfig config = {});
    ~BoundedTaskExecutor(); // Owner must outlive every kernel; self-destruction terminates.
    BoundedTaskExecutor(const BoundedTaskExecutor&) = delete;
    BoundedTaskExecutor& operator=(const BoundedTaskExecutor&) = delete;
    // Rejections preserve task.input. Admission reserves input+output+scratch.
    Submission submit(Task& task);
    TakenResult tryTake(JobKey key);
    TakenResult waitTake(JobKey key); // Same-pool kernels get WouldDeadlock.
    bool cancel(JobKey key);
    // Strictly increasing, nonzero generations. Discards old queued/completed
    // data; running old data is held until its kernel returns, then discarded.
    bool advanceGeneration(std::uint64_t generation);
    void requestStop(); // Cancels all unclaimed results too; no new admission.
    bool stopAndJoin(); // False from own kernel, with no blocking or detach.
    ExecutorStats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace concord::tasks
