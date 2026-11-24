#pragma once

#include "render/buildings/RuntimeBuildingDepthReplay.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace concord::render {

struct RuntimeBehindMarkerCandidate {
    int objectId = -1;
    RuntimeInputPixelPoint center{};
    std::array<RuntimeInputPixelPoint, 25> samples{};
    int sampleCount = 0;
    std::uint32_t covered = 0;
    bool depthSamplesEvaluated = false;
};

struct RuntimeBehindMarkerFrameState {
    std::vector<RuntimeBehindMarkerCandidate> candidates;
    std::unordered_map<int, std::size_t> candidateById;
    RuntimeBehindMarkerCandidate* drawingUnit = nullptr;
    bool drawingStructure = false;

    void addCandidate(int objectId, RuntimeInputPixelPoint center);
    void beginObject(int objectId, bool isStructure);
};

struct RuntimeBehindMarkerDepthFrame {
    RuntimeInputPixelRect viewport;
    RuntimeInputPixelRect requestedViewport;
    int width;
    bool resolved;
    bool opaqueMarkerCovers;
    const RuntimeShpFrameImage* zShape;
    std::span<const RuntimeBuildingDepthPixel> pixels;
    std::span<const RuntimeBuildingDepthDraw> draws;
    const std::unordered_set<int>& occludingStructureIds;
    const std::unordered_set<int>& frozenOwners;
};

void recordRuntimeBehindMarkerSprite(RuntimeBehindMarkerFrameState& frame, RuntimeInputPixelRect rect,
    RuntimeInputPixelRect viewport, const RuntimeShpFrameImage& image, int opacity);

void recordRuntimeBehindMarkerDepthSprite(RuntimeBehindMarkerFrameState& markers,
    const RuntimeBehindMarkerDepthFrame& frame, const RuntimeBuildingDepthObjectState* object,
    const RuntimeBuildingDepthFrame& description, RuntimeInputPixelRect imageRect,
    const RuntimeShpFrameImage& image, int opacity);

bool runtimeBehindMarkerCandidateCovered(const RuntimeBehindMarkerCandidate& unit);

} // namespace concord::render
