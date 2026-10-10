bool runtimeNuclearWarningVisible(const GameWorld& world,
    const RuntimeMapRenderContext& context, const RuntimeImpactRenderActor& actor) {
    if(actor.lifecycleState!="NuclearTargetWarning") return true;
    const auto& observer=context.visionObserverOwner;
    if(observer.empty() || equalAsciiNoCase(observer,actor.owner)
        || (context.vision && context.vision->isOwnerSharedWithObserver(observer,actor.owner))) return false;
    if(!context.rules) return false;
    // Retail AnimClass::AI (0x423B29) delegates PsiWarning visibility to
    // active sensors belonging to the observer, independently on each peer.
    for(const auto& sensor:world.getObjects()) {
        if(!sensor.alive || sensor.health<=0 || !sensor.poweredOperational
            || sensor.kind!=WorldObjectKind::Structure
            || !equalAsciiNoCase(sensor.owner,observer)) continue;
        const auto* type=context.rules->findBuildingType(sensor.type);
        if(!type || type->psychicDetectionRadius<=0) continue;
        const double dx=actor.x-sensor.x,dy=actor.y-sensor.y;
        if(dx*dx+dy*dy<=double(type->psychicDetectionRadius)*type->psychicDetectionRadius) return true;
    }
    return false;
}
