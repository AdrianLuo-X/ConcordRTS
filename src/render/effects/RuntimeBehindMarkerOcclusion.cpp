#include "render/effects/RuntimeBehindMarkerOcclusion.h"

#include <algorithm>

namespace concord::render {
namespace {

bool markerRectContains(RuntimeInputPixelRect rect, RuntimeInputPixelPoint point) noexcept {
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
}

bool covers(const RuntimeBehindMarkerDepthFrame& frame, RuntimeInputPixelPoint point, int finalOwner) {
    return runtimeBuildingDepthOwnerCoversMarkerSample(frame.draws, frame.zShape, frame.occludingStructureIds,
        frame.frozenOwners, frame.opaqueMarkerCovers, point, finalOwner);
}

} // namespace

void RuntimeBehindMarkerFrameState::addCandidate(int objectId, RuntimeInputPixelPoint center) {
    RuntimeBehindMarkerCandidate candidate;
    candidate.objectId = objectId;
    candidate.center = center;
    candidateById.emplace(candidate.objectId, candidates.size());
    candidates.push_back(candidate);
}

void RuntimeBehindMarkerFrameState::beginObject(int objectId, bool isStructure) {
    drawingUnit = nullptr;
    drawingStructure = isStructure;
    const auto found = candidateById.find(objectId);
    if (found != candidateById.end()) {
        drawingUnit = &candidates[found->second];
        drawingUnit->sampleCount = 0;
        drawingUnit->covered = 0;
        drawingUnit->depthSamplesEvaluated = false;
    }
}

void recordRuntimeBehindMarkerSprite(RuntimeBehindMarkerFrameState& frame, RuntimeInputPixelRect rect,
    RuntimeInputPixelRect viewport, const RuntimeShpFrameImage& image, int opacity) {
    if (opacity <= 0) return;
    if (frame.drawingUnit && frame.drawingUnit->sampleCount == 0) {
        auto& unit = *frame.drawingUnit;
        for (int row = 0; row < 5; ++row) for (int column = 0; column < 5; ++column) {
            const int x = std::min(image.width - 1, (2 * column + 1) * image.width / 10);
            const int y = std::min(image.height - 1, (2 * row + 1) * image.height / 10);
            const RuntimeInputPixelPoint point{rect.left + x, rect.top + y};
            if (image.pixels[static_cast<std::size_t>(y * image.width + x)] != 0
                && markerRectContains(viewport, point)) unit.samples[unit.sampleCount++] = point;
        }
        return;
    }
    if (!frame.drawingStructure || opacity < 192) return;
    for (auto& unit : frame.candidates) {
        for (int index = 0; index < unit.sampleCount; ++index) {
            const RuntimeInputPixelPoint point = unit.samples[index];
            if ((unit.covered & (1U << index)) != 0 || !markerRectContains(rect, point)) continue;
            const std::size_t pixel = static_cast<std::size_t>(point.y - rect.top) * image.width
                + static_cast<std::size_t>(point.x - rect.left);
            if (image.pixels[pixel] != 0) unit.covered |= 1U << index;
        }
    }
}

void recordRuntimeBehindMarkerDepthSprite(RuntimeBehindMarkerFrameState& markers,
    const RuntimeBehindMarkerDepthFrame& frame, const RuntimeBuildingDepthObjectState* object,
    const RuntimeBuildingDepthFrame& description, RuntimeInputPixelRect imageRect,
    const RuntimeShpFrameImage& image, int opacity) {
    if (opacity <= 0) return;
    if (markers.drawingUnit) {
        // Participating mobiles still supply their own opaque sample points.
        // Resolve hiding from the same final owner as color and picking, so a
        // foreground structure need not happen to be painted after the mobile.
        recordRuntimeBehindMarkerSprite(markers, imageRect, frame.requestedViewport, image, opacity);
        auto& unit = *markers.drawingUnit;
        if (frame.resolved && !unit.depthSamplesEvaluated) for (int index = 0; index < unit.sampleCount; ++index) {
            const auto point = unit.samples[index];
            if (!markerRectContains(frame.viewport, point)) continue;
            const auto& pixel = frame.pixels[static_cast<std::size_t>(point.y - frame.viewport.top)
                * frame.width + point.x - frame.viewport.left];
            if (covers(frame, point, pixel.visibleOwner)) unit.covered |= 1U << index;
        }
        if (frame.resolved && unit.sampleCount > 0) unit.depthSamplesEvaluated = true;
        return;
    }
    if (!markers.drawingStructure || opacity < 192) return;
    for (auto& unit : markers.candidates) for (int index = 0; index < unit.sampleCount; ++index) {
        const RuntimeInputPixelPoint point = unit.samples[index];
        if ((unit.covered & (1U << index)) || !markerRectContains(imageRect, point)
            || !markerRectContains(frame.viewport, point)) continue;
        const int x = point.x - imageRect.left, y = point.y - imageRect.top;
        if (!image.pixels[static_cast<std::size_t>(y) * image.width + x]) continue;
        int depth = 0;
        if (!runtimeBuildingRenderDepthAtPixel(description, frame.zShape->pixels,
            frame.zShape->width, frame.zShape->height, x, y, depth)) continue;
        auto previous = frame.pixels[static_cast<std::size_t>(point.y - frame.viewport.top)
            * frame.width + point.x - frame.viewport.left];
        if (frame.resolved ? previous.visibleOwner == object->instanceId
                && covers(frame, point, previous.visibleOwner)
            : runtimeBuildingDepthAccept(previous, object->instanceId, depth, false))
            unit.covered |= 1U << index;
    }
}

bool runtimeBehindMarkerCandidateCovered(const RuntimeBehindMarkerCandidate& unit) {
    int covered = 0;
    for (int index = 0; index < unit.sampleCount; ++index)
        covered += (unit.covered & (1U << index)) != 0;
    return unit.sampleCount > 0 && covered * 2 >= unit.sampleCount;
}

} // namespace concord::render
