#pragma once

struct PathCell {
    int x = -1;
    int y = -1;
};

enum class PathMovementMode {
    Ground,
    Naval,
    Amphibious,
    Air
};

struct PathTerrainCell {
    PathCell cell;
    int level = 0;
    bool passable = true;
    bool water = false;
    bool shore = false;
    bool bridge = false;
    bool ramp = false;
    bool cliff = false;
    bool resource = false;
    int cost = 10;
    int hazardCost = 0;
};

