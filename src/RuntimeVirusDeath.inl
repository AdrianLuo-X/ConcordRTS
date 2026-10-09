// VIRUSD has 35 frames: swelling through frame 20, rupture from frame 21.
// Keep the emission on this timeline instead of spawning harmful gas at death.
bool runtimeIsVirusDeathAnimation(const RuntimeWindowState& state, const RuntimeImpactEffect& impact) {
    return impact.death && !impact.animId.empty()
        && toLowerAsciiLocal(impact.animId) == toLowerAsciiLocal(state.rules.getInfantryVirusAnim());
}

void initializeRuntimeVirusDeath(RuntimeWindowState& state, RuntimeImpactEffect& impact) {
    if (!runtimeIsVirusDeathAnimation(state, impact)) return;
    const auto* art = state.art.findArtType(impact.animId);
    impact.lifecycleState = "VirusDeath";
    impact.deferredParticleAnimId = impact.animId;
    const int hz = std::max(1, state.config.fixedTickHz);
    impact.animationFrameDelayTicks = runtimeImpactAnimationFrameDelayTicks(art, hz);
    const int steps = impact.animationSequenceSteps > 0 ? impact.animationSequenceSteps : 35;
    impact.totalTicks = steps * impact.animationFrameDelayTicks;
    impact.remainingTicks = impact.totalTicks;
    impact.deferredParticleTicks = std::max(1, (steps * 3 / 5) * impact.animationFrameDelayTicks);
}

bool advanceRuntimeVirusDeath(RuntimeWindowState& state, RuntimeImpactEffect& impact) {
    if (impact.deferredParticleTicks <= 0 || --impact.deferredParticleTicks > 0) return false;
    impact.deferredParticleTicks = -1; // Consumed even when art has no particles.
    RuntimeParticleSpawnContext context;
    context.sourceObjectId = impact.attackerObjectId;
    context.owner = impact.deferredParticleOwner;
    context.sourceTag = "animation:" + impact.deferredParticleAnimId;
    context.position = {
        (impact.hasWorldPosition ? impact.worldX : impact.cell.x) * 256.0,
        (impact.hasWorldPosition ? impact.worldY : impact.cell.y) * 256.0,
        (impact.hasWorldPosition ? impact.worldLevel : impact.level) * 256.0
    };
    context.seed = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(impact.projectileActorId)) << 32U)
        ^ static_cast<std::uint32_t>(state.tick) ^ 0xd1e5ULL;
    (void)spawnRuntimeArtAnimationParticles(state, impact.deferredParticleAnimId, context);
    emitRuntimeSound(state, "sound:VirusVictimExplode", runtimeSpatialAudioPositionForImpact(impact), true);
    return true;
}
