#pragma once

// Original value identities, enumerators, layout and defaults.
// The legacy qualification is retained for the existing C++ ABI.
namespace concord::runtime::session_detail {
enum class RuntimeComputerAiStructureRole {
    Power,
    Refinery,
    Barracks,
    VehicleFactory,
    Shipyard,
    Radar,
    Defense,
    Tech,
    Superweapon
};

struct RuntimeComputerAiBaseTargets {
    int defenses = 0;
    int refineries = 1;
    bool slaveRefineries = false;
};
}
