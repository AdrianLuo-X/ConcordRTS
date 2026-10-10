// A short-lived probe follows the same SHP/VXL body draws as the compositor.
// Sampling the actual opaque unit and building pixels avoids rectangle-only
// false positives around transparent roofs, and includes animated front walls.
struct RuntimeBehindMarkerCandidate {
    int objectId = -1;
    POINT center{};
    std::array<POINT, 25> samples{};
    int sampleCount = 0;
    std::uint32_t covered = 0;
    bool depthSamplesEvaluated = false;
};

struct RuntimeBehindMarkerFrameState {
    std::vector<RuntimeBehindMarkerCandidate> candidates;
    std::unordered_map<int, std::size_t> candidateById;
    RuntimeBehindMarkerCandidate* drawingUnit = nullptr;
    bool drawingStructure = false;
};

thread_local RuntimeBehindMarkerFrameState* runtimeBehindMarkerFrame = nullptr;

struct RuntimeBehindMarkerFrameScope {
    RuntimeBehindMarkerFrameState* previous = runtimeBehindMarkerFrame;
    RuntimeBehindMarkerFrameScope(RuntimeBehindMarkerFrameState& frame,
        const RuntimeMapRenderContext& context,
        const std::vector<RuntimeWorldObjectDrawRecord>& objects) {
        runtimeBehindMarkerFrame = nullptr;
        if (!context.seeHiddenObjects || context.effectOnlyProbe) return;
        for (const auto& record : objects) {
            const auto* object = record.object;
            if (!object || !record.bodyVisible || record.airborneForeground || object->airborne
                || !object->alive || object->kind == WorldObjectKind::Structure
                || object->dockedToObjectId >= 0 || object->tankBunkerHostId >= 0
                || toLowerAscii(object->mission) == "insidetransport") continue;
            const auto* art = findArtTypeForObject(*object, context);
            if (art && !art->canBeHidden) continue;
            RuntimeBehindMarkerCandidate candidate;
            candidate.objectId = object->instanceId;
            candidate.center = {record.center.x, record.center.y - 8};
            frame.candidateById.emplace(candidate.objectId, frame.candidates.size());
            frame.candidates.push_back(candidate);
        }
        if (!frame.candidates.empty()) runtimeBehindMarkerFrame = &frame;
    }
    ~RuntimeBehindMarkerFrameScope() { runtimeBehindMarkerFrame = previous; }
};

struct RuntimeBehindMarkerObjectScope {
    RuntimeBehindMarkerFrameState* frame = runtimeBehindMarkerFrame;
    RuntimeBehindMarkerCandidate* previousUnit = nullptr;
    bool previousStructure = false;
    explicit RuntimeBehindMarkerObjectScope(const WorldObjectInstance& object) {
        if (!frame) return;
        previousUnit = frame->drawingUnit;
        previousStructure = frame->drawingStructure;
        frame->drawingUnit = nullptr;
        frame->drawingStructure = object.alive && object.kind == WorldObjectKind::Structure;
        const auto found = frame->candidateById.find(object.instanceId);
        if (found != frame->candidateById.end()) {
            frame->drawingUnit = &frame->candidates[found->second];
            frame->drawingUnit->sampleCount = 0;
            frame->drawingUnit->covered = 0;
            frame->drawingUnit->depthSamplesEvaluated = false;
        }
    }
    ~RuntimeBehindMarkerObjectScope() {
        if (!frame) return;
        frame->drawingUnit = previousUnit;
        frame->drawingStructure = previousStructure;
    }
};

void recordRuntimeBehindMarkerShpFrame(const RECT& rect, const RECT& viewport,
    const RuntimeShpFrameImage& image, int opacity) {
    auto* frame = runtimeBehindMarkerFrame;
    if (!frame || opacity <= 0) return;
    if (frame->drawingUnit && frame->drawingUnit->sampleCount == 0) {
        auto& unit = *frame->drawingUnit;
        for (int row = 0; row < 5; ++row) for (int column = 0; column < 5; ++column) {
            const int x = std::min(image.width - 1, (2 * column + 1) * image.width / 10);
            const int y = std::min(image.height - 1, (2 * row + 1) * image.height / 10);
            const POINT point{rect.left + x, rect.top + y};
            if (image.pixels[static_cast<std::size_t>(y * image.width + x)] != 0
                && PtInRect(&viewport, point)) unit.samples[unit.sampleCount++] = point;
        }
        return;
    }
    if (!frame->drawingStructure || opacity < 192) return;
    for (auto& unit : frame->candidates) {
        for (int index = 0; index < unit.sampleCount; ++index) {
            const POINT point = unit.samples[index];
            if ((unit.covered & (1U << index)) != 0 || !PtInRect(&rect, point)) continue;
            const std::size_t pixel = static_cast<std::size_t>(point.y - rect.top) * image.width
                + static_cast<std::size_t>(point.x - rect.left);
            if (image.pixels[pixel] != 0) unit.covered |= 1U << index;
        }
    }
}

bool runtimeBehindMarkerCandidateCovered(const RuntimeBehindMarkerCandidate& unit) {
    int covered = 0;
    for (int index = 0; index < unit.sampleCount; ++index)
        covered += (unit.covered & (1U << index)) != 0;
    return unit.sampleCount > 0 && covered * 2 >= unit.sampleCount;
}
