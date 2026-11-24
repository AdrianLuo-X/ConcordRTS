using concord::render::RuntimeVehicleBodyImpact;
using concord::render::RuntimeVehicleBodyPoseSources;

void prepareRuntimeVehicleBodyPoseSources(
    RuntimeVehicleBodyPoseSources& sources, const RuntimeMapRenderContext& context, bool buildIndex = true
) {
    if (context.rules == nullptr) return;
    const double hz = std::max(1, context.fixedTickHz);
    if (context.specialMechanismSnapshots != nullptr)
        sources.captureAttachments(*context.specialMechanismSnapshots, *context.rules);
    if (context.impactActors != nullptr) {
        for (const auto& impact : *context.impactActors) {
            if (!runtimeVehicleBodyHasLiveBlast(impact.damage, impact.remainingTicks, impact.totalTicks,
                impact.renderIntent, impact.lifecycleState)) continue;
            const auto* wh = context.rules->findWarheadType(impact.warheadId);
            if (!runtimeVehicleBodyWarheadCanRock(wh)) continue;
            const double directRocking = wh->rocker
                ? context.rules->getSpecialMechanismRules().directRockingCoefficient : 0.0;
            sources.impacts.push_back({&impact, runtimeVehicleBlastPresentation(
                *wh, impact.totalTicks, impact.remainingTicks, hz, directRocking)});
        }
    }
    if (sources.beginImpactIndex(buildIndex && !runtimeMovementReferenceForRender())) {
        for (const auto& source : sources.impacts) {
            const auto& impact = *source.actor;
            sources.indexImpact(impact.x, impact.y, impact.level, source.presentation.radius);
        }
    }
}

struct RuntimeVehicleBodyPoseRenderScope {
    static inline thread_local RuntimeVehicleBodyPoseRenderScope* active = nullptr;
    RuntimeVehicleBodyPoseRenderScope* previous = active;
    const RulesDatabase* rules = nullptr;
    const std::vector<RuntimeImpactRenderActor>* impacts = nullptr;
    const std::vector<SpecialMechanismRenderSnapshot>* mechanisms = nullptr;
    int fixedTickHz = 0;
    bool prepared = false;
    RuntimeVehicleBodyPoseSources sources;

    explicit RuntimeVehicleBodyPoseRenderScope(const RuntimeMapRenderContext& context)
        : rules(context.rules), impacts(context.impactActors),
          mechanisms(context.specialMechanismSnapshots), fixedTickHz(context.fixedTickHz) {
        active = this;
    }
    ~RuntimeVehicleBodyPoseRenderScope() { active = previous; }
    bool matches(const RuntimeMapRenderContext& context) const {
        return rules == context.rules && impacts == context.impactActors
            && mechanisms == context.specialMechanismSnapshots && fixedTickHz == context.fixedTickHz;
    }
    const RuntimeVehicleBodyPoseSources& get(const RuntimeMapRenderContext& context) {
        if (!prepared) {
            prepareRuntimeVehicleBodyPoseSources(sources, context);
            prepared = true;
        }
        return sources;
    }
};

RuntimeVehicleBodyPose runtimeVehicleTerrainPoseForRender(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context);

RuntimeVehicleBodyPose runtimeVehicleBodyPoseForRender(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context
) {
    RuntimeVehicleBodyPose pose;
    if (context.rules == nullptr || object.kind != WorldObjectKind::Unit
        || !object.alive || object.health <= 0 || object.dockedToObjectId >= 0) return pose;
    const auto* unit = context.rules->findUnitType("Vehicle", object.type);
    if (unit == nullptr || unit->jumpJet || unit->balloonHover) return pose;
    const RuntimeVehicleBodyPose terrainPose = runtimeVehicleTerrainPoseForRender(object, context);
    if (!unit->naval && (object.submerged || object.cloaked)) return terrainPose;
    const double hz = std::max(1, context.fixedTickHz);
    RuntimeVehicleBodyPoseSources localSources;
    auto* scope = RuntimeVehicleBodyPoseRenderScope::active;
    const bool shareSources = scope != nullptr && scope->matches(context);
    if (!shareSources) prepareRuntimeVehicleBodyPoseSources(localSources, context, false);
    const auto& sources = shareSources ? scope->get(context) : localSources;
    if (unit->naval) {
        const auto squid = sources.squidByTarget.find(object.instanceId);
        if (unit->organic || squid == sources.squidByTarget.end()) return terrainPose;
        return concord::render::runtimeVehicleGrappleHullPose(
            squid->second->progress, context.fixedTickHz, object.facing);
    }
    const auto parasite = sources.parasitesByTarget.find(object.instanceId);
    if (parasite != sources.parasitesByTarget.end()) {
        const auto& snapshot = *parasite->second;
        const int biteAge = runtimeVehicleParasiteBiteAgeTicks(snapshot.progress,
            snapshot.requiredProgress, snapshot.expireTick, context.animationTick);
        pose = runtimeVehicleParasiteMotion(biteAge / hz, unit->weight);
    }
    if (!sources.impacts.empty()) {
        const double x = object.hasRenderPosition ? object.renderX : object.x;
        const double y = object.hasRenderPosition ? object.renderY : object.y;
        const double z = object.hasRenderPosition ? object.renderLevel : object.level;
        const auto applyImpact = [&](const RuntimeVehicleBodyImpact& source) {
            const auto& impact = *source.actor;
            concord::render::accumulateRuntimeVehicleBlastPose(pose, source.presentation,
                {x, y, z}, {impact.x, impact.y, impact.level}, impact.damage, unit->weight);
        };
        if (const auto* candidates = sources.candidatesFor(x, y, z)) {
            for (const std::size_t index : *candidates) applyImpact(sources.impacts[index]);
        } else {
            for (const auto& source : sources.impacts) applyImpact(source);
        }
    }
    return concord::render::composeRuntimeGroundBodyPose(pose, terrainPose);
}

void touchRuntimeVehicleBodyVoxelCache(
    RuntimeMapRenderCache& cache, RuntimeVoxelCacheEntry& entry,
    const std::string& rasterKey, const std::string& lookupKey
) {
    concord::render::touchRuntimeVehicleBodyPoseUse(cache.vehicleBodyPoseUses,
        cache.stableVoxelCacheKeyByLookup, cache.vehicleBodyPoseUseSequence,
        entry.vehicleBodyPoseUse, rasterKey, lookupKey);
    cache.vehicleBodyPoseCacheEntries = static_cast<int>(cache.vehicleBodyPoseUses.size());
}

void trimRuntimeVehicleBodyVoxelCache(RuntimeMapRenderCache& cache) {
    concord::render::trimRuntimeVehicleBodyPoseUses(cache.vehicleBodyPoseUses,
        cache.stableVoxelCacheKeyByLookup, cache.vehicleBodyPoseCacheEvictions,
        [&](const std::string& key, std::uint64_t use) {
            const auto raster = cache.voxelObjectsByType.find(key);
            if (raster != cache.voxelObjectsByType.end()
                && raster->second.vehicleBodyPoseUse == use)
                cache.voxelObjectsByType.erase(raster);
        });
    cache.vehicleBodyPoseCacheEntries = static_cast<int>(cache.vehicleBodyPoseUses.size());
}

struct RuntimeVehicleBodyVoxelCacheDrawScope {
    RuntimeMapRenderCache& cache;
    ~RuntimeVehicleBodyVoxelCacheDrawScope() {
        // Body, turret and barrel may still borrow one another's entries while
        // drawing. Evict only after the complete object draw (including early
        // returns); ordinary facings and source VXL/HVA bytes are never removed.
        trimRuntimeVehicleBodyVoxelCache(cache);
    }
};
