#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

// Opt-in instrumentation for the production loop. Wall time never determines
// how many simulation ticks should run, except for the benchmark's timeout.
class RuntimeLiveFrameBenchmark {
public:
    static constexpr std::size_t SessionTimeSamples = 4096;
    static constexpr std::size_t SegmentTimeSamples = 512;
    static bool requested() {
        static const bool value = [] {
            const char* flag = std::getenv("CONCORD_LIVE_BENCHMARK");
            return flag != nullptr && std::strcmp(flag, "1") == 0;
        }();
        return value;
    }

    bool enabled() const { return enabled_; }

    void begin(int initialTick, int pacingHz = 30) {
        if (!enabled_) return;
        measurementTicks_ = 600;
        if (const char* text = std::getenv("CONCORD_LIVE_BENCHMARK_TICKS")) {
            char* end = nullptr;
            const long value = std::strtol(text, &end, 10);
            if (end != text && *end == '\0') {
                measurementTicks_ = static_cast<int>(std::clamp(value, 60L, 216000L));
            }
        }
        initialTick_ = initialTick;
        // Keep the short regression's original deadline; allow explicit soak
        // runs several minutes without silently stopping at the old 3600 cap.
        timeoutSeconds_ = std::max(60, (measurementTicks_ + 60) * 2 / std::max(1, pacingHz) + 30);
        // Slow reference algorithms must finish the same workload to make
        // comparisons meaningful. An explicit diagnostic timeout changes no
        // simulation budget, pacing, or success checks for completed ticks.
        if (const char* text = std::getenv("CONCORD_LIVE_BENCHMARK_TIMEOUT_SECONDS")) {
            char* end = nullptr;
            const long value = std::strtol(text, &end, 10);
            if (end != text && *end == '\0' && value > 0 && value <= 3600)
                timeoutSeconds_ = std::max(timeoutSeconds_, static_cast<int>(value));
        }
        segmentTicks_ = std::getenv("CONCORD_LIVE_BENCHMARK_BATTLE") ? 120 : 600;
        segments_.assign((measurementTicks_ + segmentTicks_ - 1) / segmentTicks_, {});
        activeSegment_ = segments_.size();
        segmentPaintSamples_.clear(); segmentGapSamples_.clear();
        segmentPaintSamples_.reserve(SegmentTimeSamples);
        segmentGapSamples_.reserve(SegmentTimeSamples);
        warmupEndTick_ = static_cast<std::int64_t>(initialTick) + 60;
        targetTick_ = warmupEndTick_ + measurementTicks_;
        lastSimulationTick_ = warmupEndTick_;
        lastPolledTick_ = initialTick;
        simulations_ = paints_ = renders_ = pendingSimulationCount_ = 0;
        visibleObjectsTotal_ = 0;
        visibleObjectsPeak_ = 0;
        maxSimulationBatchCount_ = 0;
        simulationTotalMs_ = simulationMaxMs_ = wakeTotalMs_ = 0.0;
        paintTotalMs_ = paintMaxMs_ = mapTotalMs_ = fogTotalMs_ = 0.0;
        pendingSimulationMs_ = maxSimulationBatchMs_ = 0.0;
        paintTimes_.clear();
        paintTimes_.reserve(SessionTimeSamples);
        frameGapTimes_.clear();
        frameGapTimes_.reserve(SessionTimeSamples);
        simulationTimes_.clear();
        simulationTimes_.reserve(SessionTimeSamples);
        gap50Count_ = gap100Count_ = 0;
        frameGapMaxMs_ = 0.0;
        frameGapTotalMs_ = 0.0;
        sampling_ = stopped_ = timedOut_ = false;
        sequentialTicks_ = true;
        begun_ = true;
        beganAt_ = Clock::now();
    }

    bool shouldAdvance(int currentTick) const {
        if (!enabled_) return true;
        if (!begun_ || stopped_) return false;
        // Yield exactly at the warmup boundary so poll can start the sample
        // before a catch-up batch executes its first measured simulation tick.
        return currentTick < (sampling_ ? targetTick_ : warmupEndTick_);
    }

    void recordSimulation(int completedTick, double simulationMs, double wakeMs) {
        if (!enabled_ || !sampling_ || stopped_
            || completedTick <= warmupEndTick_ || completedTick > targetTick_) return;
        sequentialTicks_ = sequentialTicks_
            && completedTick == lastSimulationTick_ + 1;
        lastSimulationTick_ = completedTick;
        ++simulations_;
        appendTimeSample(simulationTimes_, simulationMs, simulations_, SessionTimeSamples);
        simulationTotalMs_ += simulationMs;
        simulationMaxMs_ = std::max(simulationMaxMs_, simulationMs);
        auto& segment = segmentFor(completedTick);
        markSegmentActivity(segment, Clock::now());
        ++segment.simulations;
        segment.simulationMs += simulationMs;
        segment.simulationMaxMs = std::max(segment.simulationMaxMs, simulationMs);
        wakeTotalMs_ += wakeMs;
        ++pendingSimulationCount_;
        pendingSimulationMs_ += simulationMs + wakeMs;
        maxSimulationBatchCount_ = std::max(
            maxSimulationBatchCount_, pendingSimulationCount_);
        maxSimulationBatchMs_ = std::max(maxSimulationBatchMs_, pendingSimulationMs_);
    }

    void recordPaint(int currentTick, double paintMs, double mapMs,
                     double fogMs, bool rendered, int visibleObjects = 0) {
        if (!enabled_ || !sampling_ || stopped_
            || currentTick <= warmupEndTick_ || currentTick > targetTick_) return;
        ++paints_;
        const auto paintedAt = Clock::now();
        const double gapMs = seconds(paintedAt - lastPaintedAt_) * 1000.0;
        lastPaintedAt_ = paintedAt;
        appendTimeSample(frameGapTimes_, gapMs, paints_, SessionTimeSamples);
        frameGapTotalMs_ += gapMs;
        frameGapMaxMs_ = std::max(frameGapMaxMs_, gapMs);
        gap50Count_ += gapMs > 50.0;
        gap100Count_ += gapMs > 100.0;
        auto& segment = segmentFor(currentTick);
        markSegmentActivity(segment, paintedAt);
        ++segment.paints;
        segment.paintMs += paintMs;
        segment.paintMaxMs = std::max(segment.paintMaxMs, paintMs);
        segment.gapMs += gapMs;
        segment.gapMaxMs = std::max(segment.gapMaxMs, gapMs);
        segment.gap50Count += gapMs > 50.0;
        segment.gap100Count += gapMs > 100.0;
        appendTimeSample(segmentPaintSamples_, paintMs, segment.paints, SegmentTimeSamples);
        appendTimeSample(segmentGapSamples_, gapMs, segment.paints, SegmentTimeSamples);
        appendTimeSample(paintTimes_, paintMs, paints_, SessionTimeSamples);
        paintTotalMs_ += paintMs;
        paintMaxMs_ = std::max(paintMaxMs_, paintMs);
        if (rendered) {
            ++renders_;
            visibleObjectsTotal_ += visibleObjects;
            visibleObjectsPeak_ = std::max(visibleObjectsPeak_, visibleObjects);
            mapTotalMs_ += mapMs;
            fogTotalMs_ += fogMs;
        }
        // Includes every simulation tick since the previous measured paint,
        // not only the last tick of the most recent catch-up batch.
        pendingSimulationCount_ = 0;
        pendingSimulationMs_ = 0.0;
    }

    bool poll(int currentTick) {
        if (!enabled_ || !begun_) return false;
        if (stopped_) return true;
        const auto now = Clock::now();
        lastPolledTick_ = currentTick;
        timedOut_ = now - beganAt_ >= std::chrono::seconds(timeoutSeconds_);
        if (timedOut_ || currentTick >= targetTick_) {
            stopped_ = true;
            endedAt_ = now;
            if (sampling_ && activeSegment_ < segments_.size())
                markSegmentActivity(segments_[activeSegment_], now);
            return true;
        }
        if (!sampling_ && currentTick >= warmupEndTick_) {
            sampling_ = true;
            sampleBeganAt_ = now;
            lastPaintedAt_ = now;
            lastSegmentActivityAt_ = now;
            // shouldAdvance normally guarantees equality, detecting any
            // integration that accidentally skipped measured ticks here.
            sequentialTicks_ = currentTick == warmupEndTick_;
        }
        return false;
    }

    bool succeeded() const {
        return enabled_ && stopped_ && sampling_ && !timedOut_ && sequentialTicks_
            && lastPolledTick_ == targetTick_ && simulations_ == measurementTicks_
            && paints_ > 0 && renders_ > 0;
    }

    bool wantsPopulationSample(int tick) const {
        return enabled_ && sampling_ && !stopped_ && tick > warmupEndTick_ && tick <= targetTick_
            && (tick == targetTick_ || (tick - warmupEndTick_) % segmentTicks_ == 0);
    }

    void recordPopulation(int tick, std::size_t objects, std::size_t alive,
                          std::size_t projectiles, std::size_t impacts,
                          std::size_t systems, std::size_t particles,
                          std::size_t voxels, std::size_t bodyPoses,
                          std::uint64_t poseEvictions) {
        if (!wantsPopulationSample(tick)) return;
        auto& segment = segmentFor(tick);
        segment.objects = objects; segment.alive = alive;
        segment.projectiles = projectiles; segment.impacts = impacts;
        segment.systems = systems; segment.particles = particles;
        segment.voxels = voxels; segment.bodyPoses = bodyPoses;
        segment.poseEvictions = poseEvictions;
        segment.populationSampled = true;
    }

    // Observed mobile workload, sampled once per completed benchmark tick.
    // Counts remain exact; no actor IDs or per-tick snapshots are retained.
    void recordWorkload(int tick, int aliveMobile, int moving,
        std::uint64_t shots, std::uint64_t attacks, std::size_t projectiles,
        std::size_t impacts, std::size_t systems, std::size_t particles) {
        if (!enabled_ || !sampling_ || stopped_ || tick <= warmupEndTick_ || tick > targetTick_) return;
        auto& segment = segmentFor(tick);
        aliveMobile = std::max(0, aliveMobile); moving = std::max(0, moving);
        if (segment.workloadTicks == 0) segment.aliveMobileMin = aliveMobile;
        ++segment.workloadTicks;
        segment.aliveMobileTotal += static_cast<std::uint64_t>(aliveMobile);
        segment.aliveMobileMin = std::min(segment.aliveMobileMin, aliveMobile);
        segment.movingTotal += static_cast<std::uint64_t>(moving);
        segment.movingPeak = std::max(segment.movingPeak, moving);
        segment.aliveMobileEnd = aliveMobile;
        segment.movingTicks += moving > 0;
        segment.firingTicks += shots > 0;
        segment.projectileTicks += projectiles > 0;
        segment.projectilesTotal += static_cast<std::uint64_t>(projectiles);
        segment.shots += shots; segment.attacks += attacks;
        segment.projectilesPeak = std::max(segment.projectilesPeak, projectiles);
        segment.impactsPeak = std::max(segment.impactsPeak, impacts);
        segment.systemsPeak = std::max(segment.systemsPeak, systems);
        segment.particlesPeak = std::max(segment.particlesPeak, particles);
    }

    void printSummary(int pacingHz, int currentTick, std::uint64_t worldHash) const {
        if (!enabled_ || !begun_) return;
        const auto end = stopped_ ? endedAt_ : Clock::now();
        const double wallSeconds = seconds(end - beganAt_);
        const double measuredSeconds = sampling_ ? seconds(end - sampleBeganAt_) : 0.0;
        const auto rate = [measuredSeconds](int count) {
            return measuredSeconds > 0.0 ? count / measuredSeconds : 0.0;
        };
        const auto paintQuantiles = summarizeTimeSamples(paintTimes_);
        const auto gapQuantiles = summarizeTimeSamples(frameGapTimes_);
        const auto simulationQuantiles = summarizeTimeSamples(simulationTimes_);
        const auto flags = std::cout.flags();
        const auto precision = std::cout.precision();
        std::cout << std::fixed << std::setprecision(3)
            << "Live frame benchmark: passed=" << (succeeded() ? "yes" : "no")
            << " displayFpsMeasured=no pacingHz=" << pacingHz
            << " initialTick=" << initialTick_ << " warmupEndTick=" << warmupEndTick_
            << " targetTick=" << targetTick_ << " finalTick=" << currentTick
            << " requestedTicks=" << measurementTicks_ << " measuredTicks=" << simulations_
            << " wallSeconds=" << wallSeconds << " measuredSeconds=" << measuredSeconds
            << " timeoutSeconds=" << timeoutSeconds_
            << " deadlineExceeded=" << (timedOut_ ? "yes" : "no")
            << " sequentialTicks=" << (sequentialTicks_ ? "yes" : "no")
            << " paintCount=" << paints_ << " renderCount=" << renders_
            << " paintFps=" << rate(paints_) << " renderFps=" << rate(renders_)
            << " actualTickHz=" << rate(simulations_)
            << " paintMeanMs=" << (paints_ ? paintTotalMs_ / paints_ : 0.0)
            << " paintMedianMs=" << paintQuantiles.median << " paintP95Ms=" << paintQuantiles.p95
            << " paintP99Ms=" << paintQuantiles.p99 << " paintMaxMs=" << paintMaxMs_
            << " frameGapMeanMs=" << (paints_ ? frameGapTotalMs_ / paints_ : 0.0)
            << " frameGapP95Ms=" << gapQuantiles.p95 << " frameGapP99Ms=" << gapQuantiles.p99
            << " frameGapMaxMs=" << frameGapMaxMs_
            << " gapsOver50Ms=" << gap50Count_ << " gapsOver100Ms=" << gap100Count_
            << " paintQuantileSamples=" << paintTimes_.size()
            << " gapQuantileSamples=" << frameGapTimes_.size()
            << " simQuantileSamples=" << simulationTimes_.size()
            << " quantileSampled=" << (static_cast<std::size_t>(std::max(paints_, simulations_))
                > SessionTimeSamples ? "yes" : "no")
            << " timeSampleLimit=" << SessionTimeSamples << " segmentTimeSampleLimit=" << SegmentTimeSamples
            << " quantileMethod=deterministic-reservoir countsAndMeansExact=yes"
            << " mapMeanMs=" << (renders_ ? mapTotalMs_ / renders_ : 0.0)
            << " visibleObjectsMean=" << (renders_ ? static_cast<double>(visibleObjectsTotal_) / renders_ : 0.0)
            << " visibleObjectsPeak=" << visibleObjectsPeak_
            << " fogMeanMs=" << (renders_ ? fogTotalMs_ / renders_ : 0.0)
            << " simTotalMs=" << simulationTotalMs_
            << " simMeanMs=" << (simulations_ ? simulationTotalMs_ / simulations_ : 0.0)
            << " simP95Ms=" << simulationQuantiles.p95 << " simP99Ms=" << simulationQuantiles.p99
            << " simMaxMs=" << simulationMaxMs_ << " wakeTotalMs=" << wakeTotalMs_
            << " maxSimTicksBetweenPaints=" << maxSimulationBatchCount_
            << " maxSimMsBetweenPaints=" << maxSimulationBatchMs_
            << " worldHash=" << worldHash << '\n';
        for (std::size_t index = 0; index < segments_.size(); ++index) {
            auto segment = segments_[index];
            if (segment.simulations == 0) continue;
            if (index == activeSegment_) finishSegmentSamples(segment);
            const double segmentSeconds = std::max(0.0, segment.endedMs - segment.beganMs) / 1000.0;
            const auto segmentRate = [segmentSeconds](int count) {
                return segmentSeconds > 0.0 ? count / segmentSeconds : 0.0;
            };
            std::cout << "Live frame segment: index=" << index
                << " fromTick=" << warmupEndTick_ + index * segmentTicks_ + 1
                << " throughTick=" << warmupEndTick_ + index * segmentTicks_ + segment.simulations
                << " measuredTicks=" << segment.simulations
                << " wallBeginSeconds=" << segment.beganMs / 1000.0
                << " wallEndSeconds=" << segment.endedMs / 1000.0
                << " wallSeconds=" << segmentSeconds
                << " actualTickHz=" << segmentRate(segment.simulations)
                << " paintFps=" << segmentRate(segment.paints)
                << " simMeanMs=" << segment.simulationMs / segment.simulations
                << " simMaxMs=" << segment.simulationMaxMs
                << " paintCount=" << segment.paints
                << " paintMeanMs=" << (segment.paints ? segment.paintMs / segment.paints : 0.0)
                << " paintP95Ms=" << segment.paintP95Ms << " paintP99Ms=" << segment.paintP99Ms
                << " paintMaxMs=" << segment.paintMaxMs
                << " frameGapMeanMs=" << (segment.paints ? segment.gapMs / segment.paints : 0.0)
                << " frameGapMeasured=" << (segment.paints ? "yes" : "no")
                << " frameGapP95Ms=" << segment.gapP95Ms << " frameGapP99Ms=" << segment.gapP99Ms
                << " frameGapMaxMs=" << segment.gapMaxMs
                << " gapsOver50Ms=" << segment.gap50Count << " gapsOver100Ms=" << segment.gap100Count
                << " paintQuantileSamples=" << segment.paintSamples << " gapQuantileSamples=" << segment.gapSamples
                << " quantileSampled=" << (static_cast<std::size_t>(segment.paints) > SegmentTimeSamples ? "yes" : "no")
                << " workloadSampled=" << (segment.workloadTicks ? "yes" : "no")
                << " workloadTicks=" << segment.workloadTicks
                << " aliveMobileMean=" << (segment.workloadTicks ? static_cast<double>(segment.aliveMobileTotal) / segment.workloadTicks : 0.0)
                << " aliveMobileMin=" << segment.aliveMobileMin
                << " aliveMobileEnd=" << segment.aliveMobileEnd
                << " movingMean=" << (segment.workloadTicks ? static_cast<double>(segment.movingTotal) / segment.workloadTicks : 0.0)
                << " movingPeak=" << segment.movingPeak << " movingTicks=" << segment.movingTicks
                << " shots=" << segment.shots << " firingTicks=" << segment.firingTicks << " attacks=" << segment.attacks
                << " projectilesMean=" << (segment.workloadTicks ? static_cast<double>(segment.projectilesTotal) / segment.workloadTicks : 0.0)
                << " projectileTicks=" << segment.projectileTicks
                << " projectilesPeak=" << segment.projectilesPeak << " impactsPeak=" << segment.impactsPeak
                << " particleSystemsPeak=" << segment.systemsPeak << " particlesPeak=" << segment.particlesPeak
                << " populationSampled=" << (segment.populationSampled ? "yes" : "no")
                << " objects=" << segment.objects << " alive=" << segment.alive
                << " projectiles=" << segment.projectiles << " impacts=" << segment.impacts
                << " particleSystems=" << segment.systems << " particles=" << segment.particles
                << " voxels=" << segment.voxels << " bodyPoses=" << segment.bodyPoses
                << " poseEvictions=" << segment.poseEvictions << '\n';
        }
        std::cout.flags(flags);
        std::cout.precision(precision);
    }

private:
    using Clock = std::chrono::steady_clock;
    struct TimeQuantiles { double median = 0.0, p95 = 0.0, p99 = 0.0; };
    static TimeQuantiles summarizeTimeSamples(const std::vector<double>& values) {
        if (values.empty()) return {};
        // Used only at a segment boundary or while printing the final report.
        // All retained timing samples fit this fixed stack buffer.
        std::array<double, SessionTimeSamples> sorted{};
        const std::size_t count = std::min(values.size(), sorted.size());
        std::copy_n(values.begin(), count, sorted.begin());
        std::sort(sorted.begin(), sorted.begin() + count);
        return {(sorted[(count - 1) / 2] + sorted[count / 2]) / 2.0,
            sorted[(count * 95 + 99) / 100 - 1], sorted[(count * 99 + 99) / 100 - 1]};
    }
    static void appendTimeSample(std::vector<double>& samples, double value,
        int observations, std::size_t limit) {
        if (samples.size() < limit) { samples.push_back(value); return; }
        // A private, deterministic reservoir: it never reads or advances any
        // gameplay random state. Means, maxima and stall counts use all data.
        std::uint64_t mixed = static_cast<std::uint64_t>(observations) + 0x9e3779b97f4a7c15ULL;
        mixed = (mixed ^ (mixed >> 30)) * 0xbf58476d1ce4e5b9ULL;
        mixed = (mixed ^ (mixed >> 27)) * 0x94d049bb133111ebULL;
        mixed ^= mixed >> 31;
        const auto slot = static_cast<std::size_t>(mixed % static_cast<std::uint64_t>(observations));
        if (slot < limit) samples[slot] = value;
    }
    int segmentTicks_ = 600;
    struct Segment {
        int simulations = 0;
        double simulationMs = 0.0, simulationMaxMs = 0.0;
        double beganMs = 0.0, endedMs = 0.0;
        int paints = 0;
        double paintMs = 0.0, paintMaxMs = 0.0, paintP95Ms = 0.0, paintP99Ms = 0.0;
        double gapMs = 0.0, gapMaxMs = 0.0, gapP95Ms = 0.0, gapP99Ms = 0.0;
        std::size_t paintSamples = 0, gapSamples = 0;
        std::uint64_t gap50Count = 0, gap100Count = 0;
        int workloadTicks = 0, aliveMobileMin = 0, aliveMobileEnd = 0, movingPeak = 0;
        std::uint64_t aliveMobileTotal = 0, movingTotal = 0, shots = 0, attacks = 0, projectilesTotal = 0;
        int movingTicks = 0, firingTicks = 0, projectileTicks = 0;
        std::size_t projectilesPeak = 0, impactsPeak = 0, systemsPeak = 0, particlesPeak = 0;
        std::size_t objects = 0, alive = 0, projectiles = 0, impacts = 0;
        std::size_t systems = 0, particles = 0, voxels = 0, bodyPoses = 0;
        std::uint64_t poseEvictions = 0;
        bool populationSampled = false;
    };
    void finishSegmentSamples(Segment& segment) const {
        const auto paint = summarizeTimeSamples(segmentPaintSamples_);
        const auto gap = summarizeTimeSamples(segmentGapSamples_);
        segment.paintP95Ms = paint.p95; segment.paintP99Ms = paint.p99;
        segment.gapP95Ms = gap.p95; segment.gapP99Ms = gap.p99;
        segment.paintSamples = segmentPaintSamples_.size(); segment.gapSamples = segmentGapSamples_.size();
    }
    Segment& segmentFor(int tick) {
        const auto index = static_cast<std::size_t>(tick - warmupEndTick_ - 1) / segmentTicks_;
        if (index != activeSegment_) {
            if (activeSegment_ < segments_.size()) finishSegmentSamples(segments_[activeSegment_]);
            activeSegment_ = index;
            segmentPaintSamples_.clear(); segmentGapSamples_.clear();
            segments_[index].beganMs = segments_[index].endedMs =
                seconds(lastSegmentActivityAt_ - sampleBeganAt_) * 1000.0;
        }
        return segments_[index];
    }
    void markSegmentActivity(Segment& segment, Clock::time_point at) {
        segment.endedMs = seconds(at - sampleBeganAt_) * 1000.0;
        lastSegmentActivityAt_ = at;
    }
    std::vector<Segment> segments_;
    std::size_t activeSegment_ = 0;
    std::vector<double> segmentPaintSamples_, segmentGapSamples_;
    int timeoutSeconds_ = 60;
    static double seconds(Clock::duration duration) {
        return std::chrono::duration<double>(duration).count();
    }
    bool enabled_ = requested();
    bool begun_ = false, sampling_ = false, stopped_ = false;
    bool timedOut_ = false, sequentialTicks_ = true;
    int initialTick_ = 0, measurementTicks_ = 600;
    std::int64_t warmupEndTick_ = 0, targetTick_ = 0;
    std::int64_t lastSimulationTick_ = 0, lastPolledTick_ = 0;
    int simulations_ = 0, paints_ = 0, renders_ = 0;
    std::uint64_t visibleObjectsTotal_ = 0;
    int visibleObjectsPeak_ = 0;
    int pendingSimulationCount_ = 0, maxSimulationBatchCount_ = 0;
    double simulationTotalMs_ = 0.0, simulationMaxMs_ = 0.0, wakeTotalMs_ = 0.0;
    double paintTotalMs_ = 0.0, paintMaxMs_ = 0.0, mapTotalMs_ = 0.0, fogTotalMs_ = 0.0;
    double pendingSimulationMs_ = 0.0, maxSimulationBatchMs_ = 0.0;
    double frameGapTotalMs_ = 0.0, frameGapMaxMs_ = 0.0;
    std::uint64_t gap50Count_ = 0, gap100Count_ = 0;
    std::vector<double> paintTimes_, frameGapTimes_, simulationTimes_;
    Clock::time_point beganAt_{}, sampleBeganAt_{}, endedAt_{}, lastPaintedAt_{}, lastSegmentActivityAt_{};
};
