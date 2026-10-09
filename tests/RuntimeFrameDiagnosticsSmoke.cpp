#include "RuntimeFrameDiagnostics.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message, int& checks) {
    if (!condition) throw std::runtime_error(message);
    ++checks;
}

bool close(double left, double right) {
    return std::abs(left - right) < 0.000001;
}

RuntimeFrameSample frame(double gap, std::uint64_t number = 0) {
    RuntimeFrameSample sample;
    sample.frameGapMs = gap;
    sample.frameNumber = number;
    sample.simulationMs = gap * 0.75;
    sample.renderMs = gap * 0.2;
    sample.measuredStages = RuntimeFrameSimulation | RuntimeFrameRender;
    sample.tick = number * 3;
    sample.simTicks = 3;
    sample.totalObjects = 200;
    sample.aliveObjects = 190;
    sample.visibleObjects = 128;
    sample.gameSpeed = 5;
    sample.width = 1920;
    sample.height = 1080;
    return sample;
}
}

int main(int argc, char** argv) {
    try {
        int checks = 0;
        RuntimeFrameDiagnostics diagnostics;
        auto state = diagnostics.snapshot();
        require(diagnostics.enabled() && state.sessionFrames == 0 && state.rolling.fps == 0.0,
            "diagnostics must default to enabled with an empty snapshot", checks);

        // Hand-computable distribution: 95 x 10 ms, 4 x 50 ms, 1 x 200 ms.
        // Nearest-rank P95=10, P99=50; slowest 1%=200 => 5 updates/second.
        for (std::uint64_t index = 0; index < 100; ++index)
            diagnostics.observe(frame(index < 95 ? 10.0 : index < 99 ? 50.0 : 200.0, index));
        state = diagnostics.snapshot();
        require(state.sessionFrames == 100 && state.slow50Frames == 5 && state.slow100Frames == 1,
            "session counts or inclusive slow thresholds differ", checks);
        require(close(state.sessionMilliseconds, 1350.0) && close(state.rolling.meanMs, 13.5),
            "duration or arithmetic mean differs", checks);
        require(close(state.rolling.p95Ms, 10.0) && close(state.rolling.p99Ms, 50.0)
            && close(state.rolling.onePercentLowFps, 5.0), "percentiles or 1% low differ", checks);
        require(close(state.sessionFps, 100000.0 / 1350.0)
            && close(state.worstGapMs, 200.0), "session rate or worst gap differs", checks);

        // After wrap, all 600 current gaps are 20 ms; earlier stalls still
        // remain in lifetime counts and in the bounded slow-event evidence.
        for (std::uint64_t index = 0; index < 600; ++index) diagnostics.observe(frame(20.0, 100 + index));
        state = diagnostics.snapshot();
        require(state.sessionFrames == 700 && state.rolling.sampleCount == 600
            && close(state.rolling.p95Ms, 20.0) && close(state.rolling.p99Ms, 20.0)
            && close(state.rolling.onePercentLowFps, 50.0), "rolling window eviction differs", checks);
        require(state.slow100Frames == 1 && diagnostics.slowEvents().size() == 5,
            "window wrap erased session slow evidence", checks);

        RuntimeFrameDiagnostics bounded;
        for (std::uint64_t index = 0; index < 100; ++index) bounded.observe(frame(50.0 + index, index));
        require(bounded.slowEvents().size() == RuntimeFrameDiagnostics::slowEventCapacity,
            "slow-event memory limit was not enforced", checks);
        double smallest = 1000.0;
        for (const auto& event : bounded.slowEvents()) smallest = std::min(smallest, event.sample.frameGapMs);
        require(close(smallest, 118.0) && bounded.snapshot().slow50Frames == 100,
            "retained events are not the worst 32 gaps", checks);

        RuntimeFrameDiagnostics counters;
        auto sample = frame(100.0, 1);
        sample.cacheCounters = {true, 100, 50, 30, 20};
        counters.observe(sample);
        sample.cacheCounters = {true, 109, 55, 32, 22};
        counters.observe(sample);
        require(counters.snapshot().cacheGrowth.totalMisses == 9
            && counters.slowEvents()[1].cacheGrowth.voxelMisses == 5,
            "preloaded counters or cache deltas differ", checks);
        sample.cacheCounters = {true, 3, 1, 1, 1}; // resource-system reset
        counters.observe(sample);
        require(counters.snapshot().cacheGrowth.totalMisses == 12,
            "counter reset underflowed or lost post-reset misses", checks);
        sample.cacheCounters.measured = false;
        counters.observe(sample);
        sample.cacheCounters = {true, 1000, 800, 100, 100};
        counters.observe(sample);
        require(counters.snapshot().cacheGrowth.totalMisses == 12,
            "unmeasured counter interval was assigned to one frame", checks);
        RuntimeFrameDiagnostics primed;
        primed.primeCacheCounters({true, 5, 3, 1, 1});
        sample.cacheCounters = {true, 9, 5, 2, 2};
        primed.observe(sample);
        require(primed.snapshot().cacheGrowth.totalMisses == 4,
            "explicit session baseline lost first-frame cache misses", checks);

        RuntimeFrameDiagnostics invalid;
        invalid.observe(frame(0.0));
        invalid.observe(frame(-1.0));
        invalid.observe(frame(std::numeric_limits<double>::infinity()));
        invalid.observe(frame(std::numeric_limits<double>::quiet_NaN()));
        sample = frame(100.0);
        sample.simulationMs = std::numeric_limits<double>::quiet_NaN();
        sample.renderMs = -1.0;
        invalid.observe(sample);
        require(invalid.snapshot().rejectedFrames == 4 && invalid.snapshot().sessionFrames == 1,
            "invalid frame gaps polluted statistics", checks);
        require(invalid.slowEvents()[0].sample.measuredStages == 0
            && invalid.slowEvents()[0].cause == RuntimeFrameSlowCause::Unaccounted,
            "invalid stage durations were presented as measured evidence", checks);

        sample = frame(100.0);
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::Simulation,
            "simulation classification differs", checks);
        sample.simulationMs = 10.0;
        sample.renderMs = 80.0;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::Rendering,
            "render classification differs", checks);
        sample.resourceMs = 65.0;
        sample.measuredStages |= RuntimeFrameResource;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::ResourceWork,
            "resource detail classification differs", checks);
        sample.resourceMs = 0.0;
        sample.pathfindingMs = 70.0;
        sample.measuredStages |= RuntimeFramePathfinding;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::PathfindingWork,
            "pathfinding detail classification differs", checks);
        sample.pathfindingMs = 0.0;
        sample.simulationMs = 35.0;
        sample.renderMs = 35.0;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::MixedWork,
            "mixed-stage classification differs", checks);
        sample.simulationMs = 5.0;
        sample.renderMs = 5.0;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::Unaccounted,
            "uncovered time was claimed as a known stage", checks);

        struct StageExpectation {
            std::uint32_t bit;
            double RuntimeFrameSample::* field;
            RuntimeFrameSlowCause cause;
            const char* reportField;
        };
        const StageExpectation details[] = {
            {RuntimeFrameMoveCommand, &RuntimeFrameSample::moveCommandMs, RuntimeFrameSlowCause::MoveCommandWork, "moveCommandMs"},
            {RuntimeFrameSimVision, &RuntimeFrameSample::simVisionMs, RuntimeFrameSlowCause::VisionWork, "simVisionMs"},
            {RuntimeFrameSimMove, &RuntimeFrameSample::simMoveMs, RuntimeFrameSlowCause::MovementWork, "simMoveMs"},
            {RuntimeFrameSimAttack, &RuntimeFrameSample::simAttackMs, RuntimeFrameSlowCause::CombatWork, "simAttackMs"},
            {RuntimeFrameSimEconomy, &RuntimeFrameSample::simEconomyMs, RuntimeFrameSlowCause::EconomyWork, "simEconomyMs"},
            {RuntimeFrameSimEffects, &RuntimeFrameSample::simEffectsMs, RuntimeFrameSlowCause::EffectsWork, "simEffectsMs"},
            {RuntimeFrameSimGuard, &RuntimeFrameSample::simGuardMs, RuntimeFrameSlowCause::GuardWork, "simGuardMs"},
            {RuntimeFrameTerrain, &RuntimeFrameSample::terrainMs, RuntimeFrameSlowCause::TerrainWork, "terrainMs"},
            {RuntimeFrameWorldObjects, &RuntimeFrameSample::worldObjectsMs, RuntimeFrameSlowCause::WorldObjectsWork, "worldObjectsMs"},
            {RuntimeFrameFog, &RuntimeFrameSample::fogMs, RuntimeFrameSlowCause::FogWork, "fogMs"},
            {RuntimeFrameLighting, &RuntimeFrameSample::lightingMs, RuntimeFrameSlowCause::LightingWork, "lightingMs"},
            {RuntimeFrameOverlay, &RuntimeFrameSample::overlayMs, RuntimeFrameSlowCause::InteractionWork, "overlayMs"},
            {RuntimeFrameSidebar, &RuntimeFrameSample::sidebarMs, RuntimeFrameSlowCause::SidebarWork, "sidebarMs"},
        };
        for (const auto& detail : details) {
            sample = frame(100.0);
            const bool simulationDetail = (detail.bit & (RuntimeFrameSimVision | RuntimeFrameSimMove
                | RuntimeFrameSimAttack | RuntimeFrameSimEconomy | RuntimeFrameSimEffects
                | RuntimeFrameSimGuard | RuntimeFrameMoveCommand)) != 0;
            sample.simulationMs = simulationDetail ? 75.0 : 20.0;
            sample.renderMs = simulationDetail ? 20.0 : 75.0;
            const auto parentCause = simulationDetail
                ? RuntimeFrameSlowCause::Simulation : RuntimeFrameSlowCause::Rendering;
            sample.*(detail.field) = 50.0;
            sample.measuredStages |= detail.bit;
            require(RuntimeFrameDiagnostics::classify(sample) == detail.cause,
                "measured detail at the exact half-gap threshold was not classified", checks);
            RuntimeFrameDiagnostics detailDiagnostic;
            detailDiagnostic.observe(sample);
            std::ostringstream measuredReport;
            detailDiagnostic.writeReport(measuredReport);
            require(measuredReport.str().find(std::string(detail.reportField) + "=50.000") != std::string::npos,
                "measured detail was not emitted into the report", checks);
            sample.measuredStages &= ~detail.bit;
            RuntimeFrameDiagnostics missingDetail;
            missingDetail.observe(sample);
            std::ostringstream missingReport;
            missingDetail.writeReport(missingReport);
            require(missingReport.str().find(std::string(detail.reportField) + "=n/a") != std::string::npos
                && missingDetail.slowEvents()[0].cause == parentCause,
                "unmeasured detail was mistaken for timing evidence", checks);
            sample.measuredStages |= detail.bit;
            sample.*(detail.field) = std::numeric_limits<double>::quiet_NaN();
            RuntimeFrameDiagnostics badDetail;
            badDetail.observe(sample);
            require(!(badDetail.slowEvents()[0].sample.measuredStages & detail.bit)
                && badDetail.slowEvents()[0].cause == parentCause,
                "invalid detail timing was retained or classified", checks);
        }
        sample = frame(100.0);
        sample.simMoveMs = 55.0;
        sample.simAttackMs = 70.0;
        sample.measuredStages |= RuntimeFrameSimMove | RuntimeFrameSimAttack;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::CombatWork,
            "largest measured detail did not win over a smaller qualifying phase", checks);
        sample.simMoveMs = 45.0;
        sample.simAttackMs = 45.0;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::Simulation,
            "nested details were summed to manufacture a specific bottleneck", checks);

        // Command hooks accumulate until one completed paint. Zero is measured
        // when no command ran; invalid/reset-crossing timers are never borrowed.
        RuntimeFrameMoveCommandInterval commands;
        sample = frame(100.0);
        commands.writeTo(sample);
        require((sample.measuredStages & RuntimeFrameMoveCommand) && close(sample.moveCommandMs, 0.0),
            "no-command interval was mistaken for an unmeasured command stage", checks);
        auto outerCommand = commands.enter();
        auto nestedCommand = commands.enter();
        require(outerCommand.ownsTimer && !nestedCommand.ownsTimer,
            "recursive command acquired another wall timer", checks);
        commands.leave(nestedCommand, 7.0);
        commands.leave(outerCommand, 20.0);
        auto secondCommand = commands.enter();
        commands.leave(secondCommand, 30.0);
        commands.writeTo(sample);
        require(secondCommand.ownsTimer && close(sample.moveCommandMs, 50.0),
            "separate commands did not accumulate or recursive wall time counted twice", checks);
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::MoveCommandWork,
            "command at exact half-gap did not take priority over its simulation parent", checks);

        auto staleCommand = commands.enter();
        commands.reset(); // Completed paint, menu, video or invalid submission.
        auto nextCommand = commands.enter();
        commands.leave(staleCommand, 999.0);
        auto nextNestedCommand = commands.enter();
        require(nextCommand.ownsTimer && !nextNestedCommand.ownsTimer,
            "stale reset-crossing command changed the new interval's nesting", checks);
        commands.leave(nextNestedCommand, 2.0);
        commands.leave(nextCommand, 5.0);
        commands.writeTo(sample);
        require(close(sample.moveCommandMs, 5.0),
            "reset-crossing command duration was attached to the next paint", checks);
        commands.reset();
        commands.writeTo(sample);
        require(close(sample.moveCommandMs, 0.0) && (sample.measuredStages & RuntimeFrameMoveCommand),
            "completed-paint reset replayed the preceding command timing", checks);

        for (double bad : {-1.0, std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN()}) {
            commands.reset(); commands.add(10.0); commands.add(bad); commands.add(20.0);
            commands.writeTo(sample);
            require(!(sample.measuredStages & RuntimeFrameMoveCommand) && close(sample.moveCommandMs, 0.0),
                "invalid command duration produced a measured partial total", checks);
        }
        commands.reset();
        commands.add(std::numeric_limits<double>::max()); commands.add(std::numeric_limits<double>::max());
        commands.writeTo(sample);
        require(!(sample.measuredStages & RuntimeFrameMoveCommand),
            "command accumulation overflow was reported as measured", checks);
        commands.reset();
        auto interruptedCommand = commands.enter();
        commands.leave(interruptedCommand, 0.0, false);
        commands.writeTo(sample);
        require(!(sample.measuredStages & RuntimeFrameMoveCommand),
            "interrupted outer timer was mistaken for measured zero", checks);

        sample = frame(420.0, 606);
        sample.simulationMs = 397.0; sample.renderMs = 20.0;
        sample.moveCommandMs = 339.0; sample.measuredStages |= RuntimeFrameMoveCommand;
        RuntimeFrameDiagnostics commandOverlap;
        commandOverlap.observe(sample);
        require(commandOverlap.slowEvents()[0].cause == RuntimeFrameSlowCause::MoveCommandWork
            && close(commandOverlap.slowEvents()[0].unaccountedMs, 3.0),
            "nested command and simulation durations were added or command cause was hidden", checks);
        sample = frame(100.0);
        sample.simulationMs = 35.0; sample.renderMs = 5.0;
        sample.moveCommandMs = 40.0; sample.measuredStages |= RuntimeFrameMoveCommand;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::Unaccounted,
            "command and simulation were added to invent dominant measured work", checks);
        sample.moveCommandMs = 49.999;
        sample.simulationMs = 75.0;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::Simulation,
            "command below half-gap displaced its parent cause", checks);
        sample.simulationMs = 10.0; sample.renderMs = 10.0; sample.moveCommandMs = 80.0;
        RuntimeFrameDiagnostics commandOutsideSimulation;
        commandOutsideSimulation.observe(sample);
        require(close(commandOutsideSimulation.slowEvents()[0].unaccountedMs, 10.0),
            "command outside simulation was omitted from conservative accounted time", checks);
        std::ostringstream commandReport;
        commandOverlap.writeReport(commandReport);
        require(commandReport.str().find("moveCommandMs=339.000") != std::string::npos
            && commandReport.str().find("commandStage=yes") != std::string::npos
            && commandReport.str().find("移动命令规划占主要时间") != std::string::npos
            && commandReport.str().find("不能视为精确未归因时间") != std::string::npos
            && commandReport.str().find("不能证明 CPU 占用或某个单位是根因") != std::string::npos,
            "command report omitted timing/measurement/cause or overlap limits", checks);
        RuntimeFrameDiagnostics commandZero;
        sample = frame(100.0); commands.reset(); commands.writeTo(sample); commandZero.observe(sample);
        std::ostringstream zeroCommandReport; commandZero.writeReport(zeroCommandReport);
        require(zeroCommandReport.str().find("moveCommandMs=0.000") != std::string::npos
            && zeroCommandReport.str().find("commandStage=yes") != std::string::npos,
            "known-zero command stage was not reported distinctly", checks);
        RuntimeFrameDiagnostics commandMissing;
        sample = frame(100.0); sample.moveCommandMs = 90.0; commandMissing.observe(sample);
        std::ostringstream missingCommandReport; commandMissing.writeReport(missingCommandReport);
        require(missingCommandReport.str().find("moveCommandMs=n/a") != std::string::npos
            && missingCommandReport.str().find("commandStage=no") != std::string::npos
            && commandMissing.slowEvents()[0].cause == RuntimeFrameSlowCause::Simulation,
            "unmeasured command timing was presented as evidence", checks);
        RuntimeFrameDiagnostics commandRejected;
        sample = frame(0.0); sample.moveCommandMs = 90.0; sample.measuredStages |= RuntimeFrameMoveCommand;
        require(!commandRejected.observe(sample) && commandRejected.slowEvents().empty(),
            "invalid frame interval retained command cause evidence", checks);

        RuntimeFrameNestedStageInterval nestedStages;
        sample = frame(100.0);
        nestedStages.writeTo(sample);
        require((sample.measuredStages & RuntimeFrameNestedStageInterval::stages)
                == RuntimeFrameNestedStageInterval::stages
            && close(sample.radarMs, 0.0) && close(sample.radarRulesColorMs, 0.0)
            && close(sample.moveComponentMs, 0.0),
            "cache-hit/no-component interval is not measured zero", checks);
        auto radarOuter = nestedStages.enter(RuntimeFrameRadar);
        auto radarInner = nestedStages.enter(RuntimeFrameRadar);
        auto rulesInner = nestedStages.enter(RuntimeFrameRadarRulesColor);
        auto componentOuter = nestedStages.enter(RuntimeFrameMoveComponent);
        require(radarOuter.ownsTimer && !radarInner.ownsTimer && rulesInner.ownsTimer && componentOuter.ownsTimer,
            "nested stage timer ownership or independent stage depths differ", checks);
        nestedStages.leave(radarInner, 999.0); nestedStages.leave(rulesInner, 3.0);
        nestedStages.leave(componentOuter, 7.0); nestedStages.leave(radarOuter, 20.0);
        nestedStages.writeTo(sample);
        require(close(sample.radarMs, 20.0) && close(sample.radarRulesColorMs, 3.0)
            && close(sample.moveComponentMs, 7.0), "nested timings were added or counted recursively", checks);
        auto staleStage = nestedStages.enter(RuntimeFrameMoveComponent);
        nestedStages.reset();
        auto nextStage = nestedStages.enter(RuntimeFrameMoveComponent);
        nestedStages.leave(staleStage, 999.0);
        auto nextStageInner = nestedStages.enter(RuntimeFrameMoveComponent);
        require(nextStage.ownsTimer && !nextStageInner.ownsTimer,
            "old generation changed new nested-stage depth", checks);
        nestedStages.leave(nextStageInner, 20.0); nestedStages.leave(nextStage, 5.0);
        nestedStages.writeTo(sample);
        require(close(sample.moveComponentMs, 5.0) && close(sample.radarMs, 0.0),
            "discarded stage borrowed wall time across a paint/menu/video reset", checks);
        const std::uint32_t newStages[]{RuntimeFrameRadar, RuntimeFrameRadarRulesColor, RuntimeFrameMoveComponent};
        for (auto stage : newStages) {
            for (double bad : {-1.0, std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::quiet_NaN()}) {
                nestedStages.reset();
                auto good = nestedStages.enter(stage); nestedStages.leave(good, 10.0);
                auto invalidStage = nestedStages.enter(stage); nestedStages.leave(invalidStage, bad);
                auto afterInvalid = nestedStages.enter(stage); nestedStages.leave(afterInvalid, 20.0);
                nestedStages.writeTo(sample);
                require(!(sample.measuredStages & stage)
                    && (sample.measuredStages & (RuntimeFrameNestedStageInterval::stages & ~stage))
                        == (RuntimeFrameNestedStageInterval::stages & ~stage),
                    "invalid nested duration retained a partial total or invalidated unrelated stages", checks);
            }
            nestedStages.reset();
            auto interrupted = nestedStages.enter(stage); nestedStages.leave(interrupted, 0.0, false);
            nestedStages.writeTo(sample);
            require(!(sample.measuredStages & stage), "interrupted nested timer was measured zero", checks);
            nestedStages.reset();
            auto largeA = nestedStages.enter(stage); nestedStages.leave(largeA, std::numeric_limits<double>::max());
            auto largeB = nestedStages.enter(stage); nestedStages.leave(largeB, std::numeric_limits<double>::max());
            nestedStages.writeTo(sample);
            require(!(sample.measuredStages & stage), "nested accumulation overflow was measured", checks);
        }
        nestedStages.reset();
        auto unsupported = nestedStages.enter(RuntimeFrameSidebar);
        nestedStages.leave(unsupported, 99.0);
        nestedStages.writeTo(sample);
        require(!unsupported.ownsTimer && (sample.measuredStages & RuntimeFrameNestedStageInterval::stages)
                == RuntimeFrameNestedStageInterval::stages,
            "unsupported stage accessed the fixed accumulators", checks);

        sample = frame(100.0);
        sample.simulationMs = 10.0; sample.renderMs = 90.0;
        sample.sidebarMs = 80.0; sample.radarMs = 70.0; sample.radarRulesColorMs = 60.0;
        sample.measuredStages |= RuntimeFrameSidebar | RuntimeFrameRadar | RuntimeFrameRadarRulesColor;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::RadarWork,
            "dominant radar rebuild did not refine its sidebar parent", checks);
        sample.radarMs = 49.999;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::SidebarWork,
            "sub-half-gap radar or rules colors stole the sidebar cause", checks);
        sample.radarMs = 81.0;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::SidebarWork,
            "radar exceeding its parent was presented as reliable nested evidence", checks);
        sample.radarMs = 50.0;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::RadarWork,
            "radar exact half-gap threshold differs", checks);
        sample.measuredStages &= ~RuntimeFrameRadar;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::SidebarWork,
            "unmeasured radar refined a measured parent", checks);
        sample.measuredStages |= RuntimeFrameRadar;
        sample.measuredStages &= ~RuntimeFrameSidebar;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::Rendering,
            "radar with an unmeasured parent stole the existing fallback cause", checks);
        sample.measuredStages |= RuntimeFrameSidebar | RuntimeFrameWorldObjects;
        sample.worldObjectsMs = 85.0;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::WorldObjectsWork,
            "smaller radar parent stole priority from the largest existing measured detail", checks);
        sample.moveCommandMs = 90.0; sample.moveComponentMs = 60.0;
        sample.measuredStages |= RuntimeFrameMoveCommand | RuntimeFrameMoveComponent;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::MoveComponentWork,
            "dominant component did not refine command or lost original command priority", checks);
        sample.moveComponentMs = 49.999;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::MoveCommandWork,
            "sub-half-gap component was classified as dominant", checks);
        sample.moveComponentMs = 91.0;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::MoveCommandWork,
            "component exceeding its command parent was classified as reliable", checks);
        sample.moveComponentMs = 60.0;
        sample.measuredStages &= ~RuntimeFrameMoveComponent;
        require(RuntimeFrameDiagnostics::classify(sample) == RuntimeFrameSlowCause::MoveCommandWork,
            "unmeasured component changed the command cause", checks);
        sample = frame(420.0);
        sample.simulationMs = 397.0; sample.renderMs = 20.0;
        sample.moveCommandMs = 339.0; sample.moveComponentMs = 330.0;
        sample.radarMs = 8.0; sample.radarRulesColorMs = 5.0;
        sample.measuredStages |= RuntimeFrameMoveCommand | RuntimeFrameNestedStageInterval::stages;
        RuntimeFrameDiagnostics nestedOverlap;
        nestedOverlap.observe(sample);
        require(nestedOverlap.slowEvents()[0].cause == RuntimeFrameSlowCause::MoveComponentWork
            && close(nestedOverlap.slowEvents()[0].unaccountedMs, 3.0),
            "nested stages changed conservative overlap accounting", checks);
        sample.moveComponentMs = std::numeric_limits<double>::quiet_NaN();
        sample.radarMs = -1.0; sample.radarRulesColorMs = std::numeric_limits<double>::infinity();
        RuntimeFrameDiagnostics nestedInvalid;
        nestedInvalid.observe(sample);
        require(!(nestedInvalid.slowEvents()[0].sample.measuredStages & RuntimeFrameNestedStageInterval::stages)
            && nestedInvalid.slowEvents()[0].cause == RuntimeFrameSlowCause::MoveCommandWork,
            "invalid nested durations polluted classification or measurement flags", checks);
        std::ostringstream nestedReport; nestedOverlap.writeReport(nestedReport);
        require(nestedReport.str().find("moveComponentMs=330.000") != std::string::npos
            && nestedReport.str().find("radarMs=8.000") != std::string::npos
            && nestedReport.str().find("radarRulesColorMs=5.000") != std::string::npos
            && nestedReport.str().find("群体命令连通性规划占主要时间（已测阶段线索）") != std::string::npos,
            "nested fields/evidence label are missing from report", checks);
        nestedStages.reset(); sample = frame(100.0); nestedStages.writeTo(sample);
        RuntimeFrameDiagnostics nestedZero; nestedZero.observe(sample);
        std::ostringstream nestedZeroReport; nestedZero.writeReport(nestedZeroReport);
        require(nestedZeroReport.str().find("radarMs=0.000") != std::string::npos
            && nestedZeroReport.str().find("radarRulesColorMs=0.000") != std::string::npos
            && nestedZeroReport.str().find("moveComponentMs=0.000") != std::string::npos,
            "known-zero nested stages were reported as missing", checks);
        std::ostringstream nestedMissingReport; commandMissing.writeReport(nestedMissingReport);
        require(nestedMissingReport.str().find("radarMs=n/a") != std::string::npos
            && nestedMissingReport.str().find("radarRulesColorMs=n/a") != std::string::npos
            && nestedMissingReport.str().find("moveComponentMs=n/a") != std::string::npos,
            "unmeasured nested stages were reported as measured zero", checks);
        RuntimeFrameDiagnostics nestedRejected;
        sample = frame(0.0); sample.radarMs = 100.0; sample.radarRulesColorMs = 80.0;
        sample.moveComponentMs = 100.0; sample.measuredStages |= RuntimeFrameNestedStageInterval::stages;
        require(!nestedRejected.observe(sample) && nestedRejected.slowEvents().empty(),
            "invalid completed-paint interval retained nested timing evidence", checks);

        // Per-frame simulation phase values arrive already accumulated across
        // all ticks since the last paint. Keep that supplied total unchanged.
        RuntimeFrameDiagnostics multiTick;
        sample = frame(120.0, 555);
        sample.simTicks = 8;
        sample.simMoveMs = 72.0;
        sample.measuredStages |= RuntimeFrameSimMove;
        multiTick.observe(sample);
        require(multiTick.slowEvents()[0].sample.simTicks == 8
            && close(multiTick.slowEvents()[0].sample.simMoveMs, 72.0)
            && multiTick.slowEvents()[0].cause == RuntimeFrameSlowCause::MovementWork,
            "accumulated simulation phase time was divided by tick count", checks);

        RuntimeFrameDiagnostics labels;
        sample = frame(150.0, 77);
        sample.setSuspectResource("VXL\n", "C:\\private-player\\models\\mtnk.vxl\n");
        sample.hiddenWindow = true;
        labels.observe(sample);
        const auto& event = labels.slowEvents()[0];
        require(std::string(event.sample.suspectUnitType.data()) == "VXL"
            && std::string(event.sample.suspectResource.data()) == "mtnk.vxl",
            "resource labels retained paths or control characters", checks);
        sample.suspectResource.fill('x'); // caller did not provide a terminator
        labels.observe(sample);
        require(labels.slowEvents()[1].sample.suspectResource.back() == '\0',
            "untrusted fixed label is not terminated", checks);
        std::ostringstream report;
        labels.writeReport(report);
        const auto text = report.str();
        require(text.find("frame=77") != std::string::npos && text.find("size=1920x1080") != std::string::npos
            && text.find("hidden=yes") != std::string::npos && text.find("resourceKind=VXL") != std::string::npos
            && text.find("suspectResource=mtnk.vxl") != std::string::npos
            && text.find("suspectUnit=") == std::string::npos,
            "report omitted slow-frame context", checks);
        require(text.find("private-player") == std::string::npos && text.find("mapMs=n/a") != std::string::npos,
            "report leaked a path or claimed an unmeasured stage", checks);

        RuntimeFrameSessionMetadata metadata;
        RuntimeFrameSessionMetadata::setText(metadata.finalMapName, "湖心\n地图\t");
        RuntimeFrameSessionMetadata::setText(metadata.finalMapFile, "C:\\private-player\\maps\\lake.map\n");
        RuntimeFrameSessionMetadata::setText(metadata.finalMapSize, "0,0,240,240");
        RuntimeFrameSessionMetadata::setText(metadata.finalMapLocalSize, "4,6,228,224");
        RuntimeFrameSessionMetadata::setText(metadata.capturedUtc, "2026-10-01T03:15:20.000Z");
        metadata.finalTick = 600; metadata.terrainCells = 114960; metadata.finalSelected = 12;
        metadata.selectedUnresolved = 1;
        metadata.addSelection("MTNK", "Unit", true);
        metadata.addSelection("MTNK", "Unit", false);
        for (int i = 0; i < 9; ++i) metadata.addSelection("TYPE" + std::to_string(i), "Infantry", true);
        require(metadata.selectionTypeCount == 8 && metadata.selectedOther == 2
            && metadata.selectionTypes[0].count == 2 && metadata.selectionTypes[0].aliveCount == 1,
            "exit selection type groups exceeded bound or lost counts/alive distinction", checks);
        std::array<char, 6> shortUtf8{};
        RuntimeFrameSessionMetadata::setText(shortUtf8, "湖心");
        require(std::string(shortUtf8.data()) == "湖", "UTF-8 metadata clipping split a code point", checks);
        RuntimeFrameSessionMetadata::setText(shortUtf8, std::string("A\xC0\xAF\xED\xA0\x80", 6));
        require(std::string(shortUtf8.data()) == "A", "metadata retained invalid/overlong UTF-8", checks);
        std::ostringstream metadataReport; labels.writeReport(metadataReport, &metadata);
        const std::string metadataText = metadataReport.str();
        require(metadataText.starts_with(text) && metadataText.find("capturedUtc=2026-10-01T03:15:20.000Z") != std::string::npos
            && metadataText.find("finalTick=600") != std::string::npos
            && metadataText.find("finalMapName=湖心地图") != std::string::npos
            && metadataText.find("finalMapFile=lake.map") != std::string::npos
            && metadataText.find("terrainCells=114960") != std::string::npos
            && metadataText.find("finalSelected=12 selectedOther=2 selectedUnresolved=1 displayedTypes=8") != std::string::npos,
            "exit metadata omitted fields or changed existing slow-frame evidence", checks);
        require(metadataText.find("地图与选择不代表上述最慢帧的对象") != std::string::npos
            && metadataText.find("finalMap 仅为退出时地图") != std::string::npos
            && metadataText.find("private-player") == std::string::npos
            && metadataText.find("finalSelectionType=MTNK kind=Unit count=2 alive=1") != std::string::npos,
            "exit metadata leaked a path or implied a slow-frame/unit cause", checks);
        metadata.selectionTypeCount = std::numeric_limits<std::size_t>::max();
        metadata.finalMapName.fill('x'); // no terminator from an untrusted caller
        metadata.selectionTypes[7].type.fill('y');
        std::ostringstream guardedMetadata; labels.writeReport(guardedMetadata, &metadata);
        require(guardedMetadata.str().find("displayedTypes=8") != std::string::npos
            && guardedMetadata.str().size() < metadataText.size() + 512,
            "invalid metadata count or unterminated fixed label exceeded serialization bounds", checks);
        require(sizeof(RuntimeFrameSessionMetadata) <= 2048 && sizeof(RuntimeFrameNestedStageInterval) <= 128,
            "new session/stage diagnostics exceeded fixed storage budgets", checks);

        RuntimeFrameDiagnostics disabled(false);
        require(!disabled.observe(frame(200.0)) && disabled.snapshot().sessionFrames == 0,
            "disabled diagnostics still records samples", checks);
        std::string error;
        require(disabled.writeSessionReport({}, error) && error.empty(),
            "disabled report writer attempted filesystem work", checks);
        require(disabled.writeSessionReport({}, error, &metadata) && error.empty(),
            "disabled report with exit metadata attempted filesystem work", checks);
        require(sizeof(RuntimeFrameDiagnostics) < 32768,
            "diagnostic storage unexpectedly exceeds its fixed memory budget", checks);

        // Optional test-owned evidence directory: keep the report for inspection;
        // the smoke never removes files or directories.
        if (argc > 1) {
            const std::filesystem::path directory(argv[1]);
            const auto reportPath = directory / "frame-diagnostics-smoke-report.txt";
            require(labels.writeSessionReport(reportPath, error) && error.empty(),
                "session report could not create its output directory and file", checks);
            std::ifstream saved(reportPath, std::ios::binary);
            const std::string savedText((std::istreambuf_iterator<char>(saved)),
                std::istreambuf_iterator<char>());
            require(savedText == text, "on-disk UTF-8 report differs from stream output", checks);
            const auto metadataPath = directory / "frame-diagnostics-smoke-metadata.txt";
            require(labels.writeSessionReport(metadataPath, error, &metadata) && error.empty(),
                "metadata report could not be written", checks);
            std::ifstream metadataSaved(metadataPath, std::ios::binary);
            const std::string savedMetadata((std::istreambuf_iterator<char>(metadataSaved)), std::istreambuf_iterator<char>());
            require(savedMetadata == guardedMetadata.str(), "metadata on-disk UTF-8 report differs from bounded stream", checks);
            require(!labels.writeSessionReport(directory, error) && !error.empty(),
                "report open failure was not returned safely", checks);
        }

        std::cout << "Runtime frame diagnostics smoke passed: checks=" << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Runtime frame diagnostics smoke failed: " << error.what() << '\n';
        return 1;
    }
}
