struct RuntimeVehicleBodyImpact {
    const RuntimeImpactRenderActor* actor = nullptr;
    double radius = 0.0;
    double age = 0.0;
    double duration = 0.0;
    double coefficient = 0.0;
};

double runtimeVehicleBlastRockingCoefficient(const WarheadType& warhead, const RulesDatabase& rules) {
    if (warhead.rocker) return rules.getSpecialMechanismRules().directRockingCoefficient;
    // A narrow armor-piercing hit should only nudge the hull. Large area
    // explosions retain their existing response; explicit Rocker stays strong.
    return 0.65 * std::clamp(warhead.cellSpread, 0.0, 1.0);
}

bool runtimeVehicleBodyHasBlastIntent(const RuntimeImpactRenderActor& impact) {
    if (impact.renderIntent == "warheadImpact" || impact.renderIntent == "bombImpact"
        || impact.renderIntent == "deathImpact" || impact.renderIntent == "unitDestruction"
        || impact.renderIntent == "superWeaponEffect") return true;
    // Snapshot records keep the hit lifecycle and its age, but intentionally
    // omit presentation tags. Recover the same impulse without new authority.
    return (impact.renderIntent.empty() || impact.renderIntent == "impact")
        && (impact.lifecycleState == "Impact" || impact.lifecycleState == "DeathImpact");
}

struct RuntimeVehicleBodyPoseSources {
    std::unordered_map<int, const SpecialMechanismRenderSnapshot*> parasitesByTarget;
    std::unordered_map<int, const SpecialMechanismRenderSnapshot*> squidByTarget;
    std::vector<RuntimeVehicleBodyImpact> impacts;
    RuntimeRenderSpatialIndex impactIndex;
    bool impactsIndexed = false;
    mutable std::vector<std::size_t> impactCandidates;

    void prepare(const RuntimeMapRenderContext& context, bool buildIndex = true) {
        if (context.rules == nullptr) return;
        const double hz = std::max(1, context.fixedTickHz);
        if (context.specialMechanismSnapshots != nullptr) {
            for (const auto& snapshot : *context.specialMechanismSnapshots) {
                if (snapshot.kind != "parasite" || snapshot.intensity <= 0) continue;
                if (snapshot.phase == "organic") {
                    squidByTarget.try_emplace(snapshot.targetObjectId, &snapshot);
                    continue;
                }
                const auto* warhead = context.rules->findWarheadType(snapshot.resourceId);
                if (warhead != nullptr && !warhead->rocker) continue;
                // Preserve the first eligible attachment for each target.
                parasitesByTarget.try_emplace(snapshot.targetObjectId, &snapshot);
            }
        }
        if (context.impactActors != nullptr) {
            for (const auto& impact : *context.impactActors) {
                if (impact.damage <= 0 || impact.remainingTicks <= 0 || impact.totalTicks <= 0
                    || !runtimeVehicleBodyHasBlastIntent(impact)) continue;
                const auto* wh = context.rules->findWarheadType(impact.warheadId);
                if (wh == nullptr || wh->parasite || wh->mindControl || wh->temporal || wh->psychedelic
                    || wh->radiation || wh->psychicDamage || wh->electricAssault
                    || (!wh->rocker && wh->cellSpread <= 0.0)) continue;
                impacts.push_back({&impact,
                    std::clamp(std::max(wh->cellSpread, 0.5) + 1.25, 1.0, 6.0),
                    (impact.totalTicks - impact.remainingTicks) / hz,
                    std::min(0.9, impact.totalTicks / hz),
                    runtimeVehicleBlastRockingCoefficient(*wh, *context.rules)});
            }
        }
        impactsIndexed = buildIndex && !runtimeMovementReferenceForRender() && impacts.size() > 16;
        if (impactsIndexed) {
            for (const auto& source : impacts) {
                const auto& impact = *source.actor;
                // The extra cell keeps bucket bounds conservative even at
                // floating-point boundaries. Exact 3D distance remains below.
                // Invalid heights must retain the original all-source behavior.
                const double radius = std::isfinite(impact.level)
                    ? source.radius + 1.0 : std::numeric_limits<double>::quiet_NaN();
                impactIndex.add(impact.x - radius, impact.y - radius,
                    impact.x + radius, impact.y + radius);
            }
        }
    }

    const std::vector<std::size_t>* candidatesFor(double x, double y, double z) const {
        if (!impactsIndexed || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return nullptr;
        impactIndex.query(x, y, impactCandidates);
        return &impactCandidates;
    }
};

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
            sources.prepare(context);
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
    if (!shareSources) localSources.prepare(context, false);
    const auto& sources = shareSources ? scope->get(context) : localSources;
    if (unit->naval) {
        const auto squid = sources.squidByTarget.find(object.instanceId);
        if (unit->organic || squid == sources.squidByTarget.end()) return terrainPose;
        pose.roll = runtimeSquidGrapplePose(squid->second->progress, context.fixedTickHz).roll;
        pose.hullLocalRoll = true;
        pose.hullYaw = (worldObjectArtFacingBucket(object.facing, 32) - 8)
            * 6.283185307179586 / 32;
        return pose;
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
            const double dx = x - impact.x, dy = y - impact.y;
            const double distance = std::sqrt(dx * dx + dy * dy + (z - impact.level) * (z - impact.level));
            const auto impulse = runtimeVehicleBlastMotion(
                source.age, source.duration, dx, dy, distance, source.radius,
                impact.damage, unit->weight, source.coefficient);
            pose.pitch += impulse.pitch; pose.roll += impulse.roll; pose.height += impulse.height;
        };
        if (const auto* candidates = sources.candidatesFor(x, y, z)) {
            for (const std::size_t index : *candidates) applyImpact(sources.impacts[index]);
        } else {
            for (const auto& source : sources.impacts) applyImpact(source);
        }
    }
    pose.pitch = std::clamp(pose.pitch, -0.31, 0.31);
    pose.roll = std::clamp(pose.roll, -0.31, 0.31);
    pose.height = std::clamp(pose.height, 0.0, 0.22);
    // Bound the impact motion before adding the independent resting plane.
    // Otherwise a real ramp would be cropped by the old impulse-only limit.
    pose.pitch += terrainPose.pitch;
    pose.roll += terrainPose.roll;
    pose.terrainPitch = terrainPose.terrainPitch;
    pose.terrainRoll = terrainPose.terrainRoll;
    pose.terrainHeight = terrainPose.terrainHeight;
    pose.terrainSupported = terrainPose.terrainSupported;
    return pose;
}

void touchRuntimeVehicleBodyVoxelCache(
    RuntimeMapRenderCache& cache, RuntimeVoxelCacheEntry& entry,
    const std::string& rasterKey, const std::string& lookupKey
) {
    auto use = cache.vehicleBodyPoseUses.extract(entry.vehicleBodyPoseUse);
    const std::uint64_t sequence = ++cache.vehicleBodyPoseUseSequence;
    if (use.empty()) {
        cache.vehicleBodyPoseUses.emplace(sequence,
            RuntimeVehicleBodyPoseCacheReference{rasterKey, lookupKey});
    } else {
        // Exact scale lookup keys can alias the same raster key. Keep only the
        // current alias so eviction also bounds the secondary lookup cache.
        auto& reference = use.mapped();
        if (!lookupKey.empty() && reference.lookupKey != lookupKey) {
            const auto old = cache.stableVoxelCacheKeyByLookup.find(reference.lookupKey);
            if (old != cache.stableVoxelCacheKeyByLookup.end()
                && old->second == reference.rasterKey)
                cache.stableVoxelCacheKeyByLookup.erase(old);
            reference.lookupKey = lookupKey;
        }
        use.key() = sequence;
        cache.vehicleBodyPoseUses.insert(std::move(use));
    }
    entry.vehicleBodyPoseUse = sequence;
    cache.vehicleBodyPoseCacheEntries = static_cast<int>(cache.vehicleBodyPoseUses.size());
}

void trimRuntimeVehicleBodyVoxelCache(RuntimeMapRenderCache& cache) {
    constexpr std::size_t capacity = 768;
    while (cache.vehicleBodyPoseUses.size() > capacity) {
        const auto oldest = cache.vehicleBodyPoseUses.begin();
        const auto& reference = oldest->second;
        const auto raster = cache.voxelObjectsByType.find(reference.rasterKey);
        if (raster != cache.voxelObjectsByType.end()
            && raster->second.vehicleBodyPoseUse == oldest->first)
            cache.voxelObjectsByType.erase(raster);
        const auto lookup = cache.stableVoxelCacheKeyByLookup.find(reference.lookupKey);
        if (lookup != cache.stableVoxelCacheKeyByLookup.end()
            && lookup->second == reference.rasterKey)
            cache.stableVoxelCacheKeyByLookup.erase(lookup);
        cache.vehicleBodyPoseUses.erase(oldest);
        ++cache.vehicleBodyPoseCacheEvictions;
    }
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
