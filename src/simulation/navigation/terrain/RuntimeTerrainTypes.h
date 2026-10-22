#pragma once

// Single original enum definitions; simulation values retain every original ordinal.
enum class RuntimeTerrainLandType : int {
    Unknown = -1,
    Clear = 0,
    Road = 1,
    Water = 2,
    Rock = 3,
    Wall = 4,
    Tiberium = 5,
    Beach = 6,
    Rough = 7,
    Ice = 8,
    Railroad = 9,
    Tunnel = 10
};

enum class RuntimeTerrainKind {
    Land,
    Water,
    Shore,
    Cliff,
    Ramp,
    Bridge,
    Blocked
};

// Detached owned query result. The private navigation map keeps its tracked field.
struct RuntimeNavigationTerrainInfo {
    RuntimeTerrainLandType movementLandType = RuntimeTerrainLandType::Unknown;
    unsigned char movementRampType = 0;
    RuntimeTerrainKind kind = RuntimeTerrainKind::Land;
    RuntimeTerrainLandType authoredLandType = RuntimeTerrainLandType::Unknown;
    bool passable = true;
    bool water = false;
    bool shore = false;
    bool cliff = false;
    bool ramp = false;
    bool bridge = false;
    int cost = 10;
};
