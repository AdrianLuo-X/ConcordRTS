// The original benchmark uses read-only standard getenv; this small fixture
// opts out of Microsoft's replacement-API annotation, retaining /W4 /WX.
#define _CRT_SECURE_NO_WARNINGS
#include "runtime/performance/diagnostics/RuntimeLiveFrameBenchmark.h"
#include <sstream>
#include <string>

int main() {
    RuntimeLiveFrameBenchmark benchmark;
    if (!benchmark.enabled() || !benchmark.visibleRequested()) return 1;
    benchmark.begin(100, 30);
    // Warmup observations must not leak into any measured distribution.
    benchmark.recordSimulation(160, 9999.0, 0.0);
    benchmark.recordPaint(160, 9999.0, 0.0, 0.0, true, 1, 9999.0, true);
    if (benchmark.shouldAdvance(160) || benchmark.poll(160)) return 2;
    for (int tick = 161; tick <= 220; ++tick) {
        benchmark.recordSimulation(tick, static_cast<double>(tick - 160), 0.0);
        benchmark.recordPaint(tick, 10.0, 5.0, 1.0, true, 4, 2.0, tick != 180);
    }
    if (!benchmark.poll(220) || !benchmark.succeeded()) return 3;
    std::ostringstream output;
    auto* previous = std::cout.rdbuf(output.rdbuf());
    benchmark.printSummary(30, 220, 123456789U);
    std::cout.rdbuf(previous);
    const auto text = output.str();
    for (const auto* required : {
            "warmupEndTick=160 targetTick=220 finalTick=220",
            "measuredTicks=60", "simMedianMs=30.500 simP95Ms=57.000 simP99Ms=60.000",
            "submissionMedianMs=2.000", "cpuPreparationMedianMs=8.000",
            "visibleSubmissions=59 rejectedVisibleSubmissions=1 visibleGapSamples=58",
            "gpuGameTime=not-applicable", "displayFpsMeasured=no", "worldHash=123456789" }) {
        if (text.find(required) == std::string::npos) {
            std::cerr << "Missing timing contract: " << required << '\n' << text;
            return 4;
        }
    }
    if (text.find("9999.000") != std::string::npos) return 5;
    std::cout << "S33 timing contract: warmup, sequential ticks, nearest-rank tails, "
        "median, CPU submission separation, visibility rejection and GPU-not-applicable passed\n";
    return 0;
}
