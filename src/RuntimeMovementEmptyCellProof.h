#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <unordered_map>

// Derived metadata only. It is valid solely while the existing tick occupancy
// cache is current; it is never serialized or used as simulation authority.
struct RuntimeMovementEmptyCellProofState {
    bool valid = false;
    bool subcellCompatible = false;
    std::vector<std::size_t> subcellAirObjectIndices;
    // Ground active/stopped pose candidates; read current object pose at query time.
    std::vector<std::size_t> additionalGroundPoseObjectIndices;
    std::unordered_map<int,std::vector<std::size_t>> additionalGroundPoseIndicesByCell;
    std::vector<unsigned char> additionalGroundPoseIndexedObjects;
    bool additionalGroundPoseIndexReady = false;
    // Complete ground bodies for factory admission; not serialized or hashed.
    std::unordered_map<int,std::vector<std::size_t>> allGroundBodyIndicesByCell;
    bool allGroundBodyIndexReady = false;
    const void* objectData = nullptr;
    const void* terrainObjectData = nullptr;
    const void* terrainCellData = nullptr;
    const void* mapFactCellData = nullptr;
    std::size_t objectCount = 0;
    std::size_t terrainObjectCount = 0;
    std::size_t terrainCellCount = 0;
    std::size_t mapFactCellCount = 0;
    std::vector<std::array<std::int64_t, 4>> unindexedFoundations;
};
