bool emitRuntimeMissileExhaust(RuntimeWindowState& state, const std::string& animation,
    const std::string& owner, int sourceId, const std::string& missileType,
    const std::array<double, 3>& tail, int elapsedTicks,
    const RuntimeParticleSpriteAttachment& attachment = {}) {
    const auto* art = state.art.findArtType(animation);
    if (art == nullptr || elapsedTicks < 0) return false;
    const int frameDelay = runtimeMissileExhaustFrameDelay(art->rate, state.config.fixedTickHz);
    const int cadence = runtimeMissileExhaustEmissionDelay(art->rate, state.config.fixedTickHz);
    if (elapsedTicks % cadence != 0) return false;
    RuntimeImpactEffect exhaust;
    exhaust.attackerObjectId = sourceId; exhaust.owner = owner;
    exhaust.hasWorldPosition = true;
    exhaust.worldX = tail[0]; exhaust.worldY = tail[1]; exhaust.worldLevel = tail[2];
    exhaust.screenOffsetX = attachment.screenOffsetX;
    exhaust.screenOffsetY = attachment.screenOffsetY;
    exhaust.nativeFixedX = attachment.nativeFixedX;
    exhaust.nativeFixedY = attachment.nativeFixedY;
    exhaust.cell = {static_cast<int>(std::lround(tail[0])), static_cast<int>(std::lround(tail[1]))};
    exhaust.level = static_cast<int>(std::lround(tail[2]));
    exhaust.lifecycleState = "Trailer"; exhaust.renderIntent = "missileExhaust";
    exhaust.projectileId = missileType; exhaust.animId = animation;
    exhaust.trailerIntentId = animation;
    initializeRuntimeImpactAnimationTimeline(state, exhaust, true);
    if (exhaust.animationFrameCount <= 0) return false;
    // Frames 0..2 form the continuously attached nozzle flame in the renderer.
    // Detached puffs continue from frame 3 through the original final frame.
    exhaust.animationStartFrame = std::min(runtimeMissileExhaustFlameFrames,
        exhaust.animationFrameCount - 1);
    exhaust.animationLoopStartFrame = exhaust.animationStartFrame;
    exhaust.animationLoopEndFrame = exhaust.animationEndFrame = exhaust.animationFrameCount;
    exhaust.animationLoopCount = 1;
    exhaust.animationSequenceSteps = exhaust.animationFrameCount - exhaust.animationStartFrame;
    exhaust.frameIndex = exhaust.animationStartFrame;
    exhaust.animationFrameDelayTicks = frameDelay;
    exhaust.totalTicks = exhaust.animationSequenceSteps * frameDelay;
    exhaust.remainingTicks = exhaust.totalTicks;
    state.impactEffects.push_back(std::move(exhaust));
    return true;
}

bool emitRuntimeOrdinaryMissileExhaust(RuntimeWindowState& state,
    const RuntimeProjectileEffect& missile, double previousX, double previousY, double previousZ) {
    if (runtimeProjectileIsNuclearDeathDetonation(missile)) return false;
    const auto* type = state.rules.findProjectileType(missile.projectileId);
    const std::string animation = runtimeMissileExhaustImage(type, missile.projectileImageId);
    if (animation.empty() || missile.remainingTicks <= 0) return false;
    const double dx = missile.currentX - previousX, dy = missile.currentY - previousY;
    const double dz = (missile.currentZ - previousZ) / runtimeMissileLevelsPerCell;
    if (std::hypot(std::hypot(dx, dy), dz) < 0.000001) return false;
    const auto* large = state.rules.findSpawnedMissileProfile(missile.projectileId);
    const double bodyLength = large == nullptr ? 32.0 : large->bodyLength;
    const auto origin = animation == "NUKEPUFF"
        ? std::array<double, 3>{missile.currentX, missile.currentY, missile.currentZ}
        : runtimeMissileExhaustOrigin(missile.currentX, missile.currentY,
            missile.currentZ, std::atan2(dy, dx), std::atan2(dz, std::hypot(dx, dy)), bodyLength);
    const bool emitted = emitRuntimeMissileExhaust(state, animation, missile.owner, missile.attackerObjectId,
        missile.projectileId, origin, std::max(0, missile.elapsedTicks - 1));
    if (emitted && animation == "NUKEPUFF") state.impactEffects.back().projectileImageId = missile.projectileImageId;
    return emitted;
}

bool emitRuntimeSuperWeaponMissileExhaust(RuntimeWindowState& state) {
    bool changed = false;
    // Nuclear missiles use the super-weapon presentation path rather than
    // the ordinary weapon's projectile collection. Share the original puff.
    for (const auto& missile : state.superWeapons.renderSnapshots()) {
        if (missile.kind != "superWeaponProjectile") continue;
        const auto pose = runtimeSuperWeaponNuclearFlightPose(state, missile);
        if (!pose.visible) continue;
        const auto* type = runtimeNuclearMissileProjectile(state.rules, missile.resourceId);
        if (!type || runtimeMissileExhaustImage(type, type->image) != "NUKEPUFF") continue;
        if (emitRuntimeMissileExhaust(state, "NUKEPUFF", missile.owner, missile.sourceObjectId,
                type->id, {pose.x, pose.y, pose.level}, missile.progress)) {
            state.impactEffects.back().projectileImageId = type->image;
            changed = true;
        }
    }
    return changed;
}
