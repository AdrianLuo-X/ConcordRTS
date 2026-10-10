// A closed hatch hides the aircraft completely. After opening, use the retail
// front-wall layer, shared with hit testing, instead of a rectangular body cut.
RECT runtimeFactoryAirExitBodyViewport(const RuntimeWorldObjectDrawRecord& aircraft,
    const std::vector<RuntimeWorldObjectDrawRecord>& structures,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, const RECT& viewport) {
    RECT clipped = viewport;
    if (!aircraft.object || aircraft.object->factoryExitHostId < 0) return clipped;
    for (const auto& record : structures) {
        if (!record.object || record.object->instanceId != aircraft.object->factoryExitHostId
            || !record.object->alive || !runtimeAircraftOverStructureFoundation(aircraft, record)) continue;
        const auto& host = *record.object;
        const int opening = runtimeFactoryAirExitTransitionTicks(
            findArtTypeForObject(host, context), context.art, context.fixedTickHz);
        if (context.animationTick - host.factoryExitAnimationStartTick < opening) {
            clipped.bottom = clipped.top;
        }
        // Once open, the native foreground roof/wall supplies the occlusion.
        // A horizontal crop slices a long airship even through the open hatch.
        break;
    }
    return clipped;
}
