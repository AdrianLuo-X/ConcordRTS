#pragma once

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <unordered_map>

// Optional diagnostics for frame spikes. Disabled scopes neither read the
// clock nor allocate. Timing never participates in simulation decisions.
class RuntimePerfTraceScope {
public:
    RuntimePerfTraceScope(const char* phase, int tick) : phase_(phase), tick_(tick) {
        if (enabled()) started_ = Clock::now();
    }
    ~RuntimePerfTraceScope() {
        if (!enabled()) return;
        const double elapsed = std::chrono::duration<double, std::milli>(
            Clock::now() - started_).count();
        if (aggregateEnabled()) {
            // One report per phase at thread exit avoids thousands of console
            // writes disturbing the very frame stalls being investigated.
            auto& sample = aggregate().phases[phase_];
            ++sample.count;
            sample.totalMs += elapsed;
            if (elapsed > sample.maxMs) sample.maxMs = elapsed;
            return;
        }
        if (elapsed >= minimumMilliseconds()) {
            std::cerr << "Runtime perf: tick=" << tick_ << " phase=" << phase_
                      << " ms=" << elapsed << '\n';
        }
    }
private:
    using Clock = std::chrono::steady_clock;
    struct Sample {
        std::uint64_t count = 0;
        double totalMs = 0.0;
        double maxMs = 0.0;
    };
    struct Aggregate {
        // Phase names are static string literals; no per-scope string copies.
        std::unordered_map<const char*, Sample> phases;
        ~Aggregate() {
            for (const auto& [phase, sample] : phases) {
                std::cerr << "Runtime perf aggregate: phase=" << phase
                    << " count=" << sample.count << " totalMs=" << sample.totalMs
                    << " maxMs=" << sample.maxMs << '\n';
            }
        }
    };
    static Aggregate& aggregate() {
        static thread_local Aggregate value;
        return value;
    }
    static bool aggregateEnabled() {
        static const bool value = std::getenv("CONCORD_RUNTIME_PERF_AGGREGATE") != nullptr;
        return value;
    }
    static bool enabled() {
        static const bool value = std::getenv("CONCORD_RUNTIME_PERF_TRACE") != nullptr;
        return value;
    }
    static double minimumMilliseconds() {
        static const double value = [] {
            const char* configured = std::getenv("CONCORD_RUNTIME_PERF_TRACE_MIN_MS");
            if (configured == nullptr) return 5.0;
            char* end = nullptr;
            const double parsed = std::strtod(configured, &end);
            return end != configured && *end == '\0' && std::isfinite(parsed)
                && parsed >= 0.0 ? parsed : 5.0;
        }();
        return value;
    }
    const char* phase_;
    int tick_;
    Clock::time_point started_{};
};
