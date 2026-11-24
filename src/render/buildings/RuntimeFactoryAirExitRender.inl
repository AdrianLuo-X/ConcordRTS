RuntimeInputPixelRect runtimeFactoryAirExitBodyViewport(const RuntimeWorldObjectDrawRecord& aircraft,
    const std::vector<RuntimeWorldObjectDrawRecord>& structures,
    const RuntimeMapRenderContext& context, RuntimeInputPixelRect viewport) {
    const int hostId = aircraft.object ? aircraft.object->factoryExitHostId : -1;
    return concord::render::clipRuntimeFactoryAirExitBody(viewport, hostId, context.animationTick,
        std::span<const RuntimeWorldObjectDrawRecord>{structures},
        [&](const RuntimeWorldObjectDrawRecord& record, int id)
            -> std::optional<concord::render::RuntimeFactoryAirExitTransition> {
        if (!record.object || record.object->instanceId != id || !record.object->alive
            || !runtimeAircraftOverStructureFoundation(aircraft, record)) return std::nullopt;
        const auto& host = *record.object;
        const int opening = runtimeFactoryAirExitTransitionTicks(
            findArtTypeForObject(host, context), context.art, context.fixedTickHz);
        return concord::render::RuntimeFactoryAirExitTransition{host.factoryExitAnimationStartTick, opening};
    });
}
