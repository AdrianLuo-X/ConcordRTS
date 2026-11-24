// A short-lived probe follows the same SHP/VXL body draws as the compositor.
// Sampling the actual opaque unit and building pixels avoids rectangle-only
// false positives around transparent roofs, and includes animated front walls.
using concord::render::RuntimeBehindMarkerCandidate;
using concord::render::RuntimeBehindMarkerFrameState;
using concord::render::runtimeBehindMarkerCandidateCovered;

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
            frame.addCandidate(object->instanceId, {record.center.x, record.center.y - 8});
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
        frame->beginObject(object.instanceId, object.alive && object.kind == WorldObjectKind::Structure);
    }
    ~RuntimeBehindMarkerObjectScope() {
        if (!frame) return;
        frame->drawingUnit = previousUnit;
        frame->drawingStructure = previousStructure;
    }
};

void recordRuntimeBehindMarkerShpFrame(const RECT& rect, const RECT& viewport,
    const RuntimeShpFrameImage& image, int opacity) {
    if (auto* frame = runtimeBehindMarkerFrame)
        concord::render::recordRuntimeBehindMarkerSprite(*frame, runtimeRenderPlacementPixelRect(rect),
            runtimeRenderPlacementPixelRect(viewport), image, opacity);
}
