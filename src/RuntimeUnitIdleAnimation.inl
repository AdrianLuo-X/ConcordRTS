// Idle animation is presentation-only and follows simulation time, so pause,
// replay and repeated paints of the same tick all show the same pose.
int runtimeUnitAnimationTick(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context
) {
    if (!object.alive) return 0;
    if (context.specialMechanismSnapshots != nullptr) {
        for (const auto& snapshot : *context.specialMechanismSnapshots) {
            if (snapshot.kind == "chrono" && snapshot.objectId == object.instanceId
                && (snapshot.phase == "WarpIn" || snapshot.phase == "Cooldown"))
                return std::max(0, context.animationTick - snapshot.progress);
        }
    }
    const std::string mission = toLowerAscii(object.mission);
    if (mission == "chronofrozen" || mission == "temporal"
        || mission == "parasiteparalyzed" || mission == "parasitestunned") return 0;
    return std::max(0, context.animationTick);
}

int runtimeUnitAnimationStep(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context, int rulesFrameDelay
) {
    const std::int64_t tick = runtimeUnitAnimationTick(object, context);
    const std::int64_t divisor = static_cast<std::int64_t>(std::max(1, context.fixedTickHz))
        * std::max(1, rulesFrameDelay);
    return static_cast<int>((tick * 15) / divisor);
}

int runtimeUnitIdleFrameOffset(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context,
    const ArtType* art, int frames
) {
    if (frames <= 1) return 0;
    const UnitType* unit = findObjectUnitTypeForRender(context, object);
    if (unit != nullptr && unit->idleRate > 0)
        return runtimeUnitAnimationStep(object, context, unit->idleRate) % frames;
    if (art == nullptr || art->standingFrames <= 1) return 0;
    const std::int64_t tick = runtimeUnitAnimationTick(object, context);
    const int delay = runtimeInfantrySequenceDelayTicks(art, context.fixedTickHz);
    return static_cast<int>((tick / delay) % frames);
}
