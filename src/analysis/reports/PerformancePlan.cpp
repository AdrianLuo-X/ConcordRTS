#include "analysis/reports/PerformancePlan.h"

#include <algorithm>
#include <thread>

namespace {

int detectHardwareThreads() {
    const unsigned int detected = std::thread::hardware_concurrency();
    if (detected == 0) {
        return 1;
    }

    return static_cast<int>(std::min<unsigned int>(detected, 128));
}

int chooseWorkerThreadCount(
    int hardwareThreads,
    int requestedWorkerThreads
) {
    const int maxWorkerThreads = std::max(1, hardwareThreads - 1);

    if (requestedWorkerThreads > 0) {
        return std::clamp(requestedWorkerThreads, 1, maxWorkerThreads);
    }

    return std::max(1, std::min(4, maxWorkerThreads));
}

} // namespace

PerformancePlanReport buildPerformancePlan(const PerformancePlanConfig& config) {
    PerformancePlanReport report;
    report.hardwareThreads = detectHardwareThreads();
    report.workerThreads =
        chooseWorkerThreadCount(report.hardwareThreads, config.requestedWorkerThreads);

    report.subsystems = {
        {
            "Core simulation",
            "guarded",
            "Main simulation thread",
            "Run fixed ticks on one authoritative thread; worker results are immutable proposals.",
            "Only the main tick commits commands, damage, economy state, and random decisions.",
            "Add a command/result queue keyed by tick id before moving any work off-thread."
        },
        {
            "Renderer",
            "yes",
            "Render thread",
            "Consume immutable render snapshots produced after each committed simulation tick.",
            "Renderer never owns gameplay state and may drop stale snapshots when overloaded.",
            "Split visible sprite/tile snapshot generation from HTML/debug render output."
        },
        {
            "Resource loading",
            "yes",
            "IO lane",
            "Read MIX archives, maps, INI files, and loose assets asynchronously into byte buffers.",
            "Loaded bytes become immutable resources before the simulation can reference them.",
            "Introduce an async resource request record with status, error, and source path."
        },
        {
            "Resource decoding",
            "yes",
            "Worker pool",
            "Decode PAL/SHP/VXL/HVA/TMP data as independent jobs after IO completes.",
            "Decoded assets are published by handle version, never mutated in place.",
            "Batch tile and sprite decode jobs by theater/resource type."
        },
        {
            "Pathfinding",
            "yes",
            "Pathfinding lane",
            "Compute path candidates in the background using a read-only map/nav snapshot.",
            "The main tick validates and commits the selected path against current blockers.",
            "Cache nav snapshots by map revision and invalidate them on bridge/building changes."
        },
        {
            "AI scoring",
            "yes",
            "AI lane",
            "Score candidate build, attack, and trigger choices off-thread from a snapshot.",
            "The main tick chooses and commits actions in a stable order.",
            "Return sorted candidate lists with deterministic tie breakers."
        },
        {
            "Audio",
            "yes",
            "Audio thread",
            "Consume fire-and-forget sound events and stream playback outside simulation ticks.",
            "Audio events carry ids and positions; playback cannot feed back into simulation.",
            "Add a bounded event queue and drop low-priority events under pressure."
        }
    };

    report.lanes = {
        {
            "MainSimulation",
            "Owns deterministic gameplay state and commits all authoritative changes.",
            {
                "fixed tick advance",
                "command queue drain",
                "worker result validation",
                "snapshot publish"
            }
        },
        {
            "RenderThread",
            "Draws the latest immutable snapshot without blocking the next simulation tick.",
            {
                "sprite sorting",
                "tile layer batching",
                "debug overlay rendering"
            }
        },
        {
            "ResourceIO",
            "Performs file and MIX archive reads without stalling gameplay or preview export.",
            {
                "MIX lookup",
                "loose file read",
                "map/art/rules preload"
            }
        },
        {
            "WorkerPool",
            "Runs CPU-heavy decode and analysis jobs that have no direct gameplay writes.",
            {
                "TMP decode",
                "SHP frame decode",
                "VXL/HVA inspection",
                "map preview tile batches"
            }
        },
        {
            "PathfindingLane",
            "Keeps long path searches away from the main tick while preserving commit order.",
            {
                "nav snapshot build",
                "path candidate search",
                "bridge/passability refresh"
            }
        },
        {
            "AIScoringLane",
            "Computes expensive AI choices from a read-only world snapshot.",
            {
                "target scoring",
                "attack wave composition",
                "build queue candidates"
            }
        },
        {
            "AudioThread",
            "Handles playback timing and stream work independently from simulation timing.",
            {
                "sound event consume",
                "voice priority trim",
                "music stream update"
            }
        }
    };

    report.risks = {
        {
            "high",
            "Determinism",
            "Never let worker completion order decide gameplay; sort and commit by tick/id."
        },
        {
            "high",
            "State ownership",
            "Use snapshots, immutable assets, and main-thread commits instead of shared mutable objects."
        },
        {
            "medium",
            "Resource lifetime",
            "Publish decoded assets through stable handles so render/path jobs cannot keep dangling pointers."
        },
        {
            "medium",
            "Over-subscription",
            "Default workers leave at least one hardware thread for UI/OS and accept --workers for testing."
        },
        {
            "medium",
            "Bridge and map revisions",
            "Pathfinding jobs must validate revision ids before committing paths on destructible terrain."
        }
    };

    report.milestones = {
        {
            "Baseline instrumentation",
            {
                "add per-command timings",
                "record asset decode counts",
                "track peak pending jobs"
            }
        },
        {
            "Job system skeleton",
            {
                "bounded worker pool",
                "job futures/results",
                "main-thread commit queue"
            }
        },
        {
            "Async resource pipeline",
            {
                "IO request queue",
                "decode jobs",
                "immutable asset publication"
            }
        },
        {
            "Parallel candidates",
            {
                "pathfinding snapshot jobs",
                "AI scoring jobs",
                "deterministic result ordering"
            }
        },
        {
            "Renderer split",
            {
                "render snapshots",
                "sprite/tile batch cache",
                "frame pacing metrics"
            }
        }
    };

    return report;
}
