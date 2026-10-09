#pragma once

#include <string>
#include <vector>

struct PerformancePlanConfig {
    int requestedWorkerThreads = 0;
};

struct PerformanceSubsystemPlan {
    std::string name;
    std::string parallelism;
    std::string owner;
    std::string strategy;
    std::string deterministicBoundary;
    std::string firstStep;
};

struct PerformanceThreadLane {
    std::string name;
    std::string purpose;
    std::vector<std::string> jobs;
};

struct PerformanceRisk {
    std::string severity;
    std::string area;
    std::string mitigation;
};

struct PerformanceMilestone {
    std::string name;
    std::vector<std::string> checks;
};

struct PerformancePlanReport {
    int hardwareThreads = 1;
    int workerThreads = 1;
    bool deterministicSimulationMainThread = true;
    bool renderThreadPlanned = true;
    bool asyncLoadingPlanned = true;
    bool jobSystemPlanned = true;
    std::vector<PerformanceSubsystemPlan> subsystems;
    std::vector<PerformanceThreadLane> lanes;
    std::vector<PerformanceRisk> risks;
    std::vector<PerformanceMilestone> milestones;
};

PerformancePlanReport buildPerformancePlan(const PerformancePlanConfig& config);
