#pragma once
#include "simulation/production/RuntimeBuildKind.h"
#include <optional>
#include <string>

// Original complete catalog value, also borrowed by the selection eligibility query.
struct RuntimeBuildCatalogItem {
    RuntimeBuildKind kind = RuntimeBuildKind::Building;
    std::string typeId;
    std::string label;
    std::string owner;
    std::string requiredHouses;
    std::string forbiddenHouses;
    std::string prerequisite;
    std::string prerequisiteOverride;
    std::string requiredFactory;
    int cost = 0;
    int buildTicks = 1;
    int powerDelta = 0;
    int techLevel = 0;
    std::optional<int> buildLimit;
    bool waterBound = false;
    bool naval = false;
    bool wall = false;
    bool techTreeAllowed = true;
    bool secretLabInjected = false;
    std::string techTreeKind;
    std::string techTreeBlockedReason;
};
