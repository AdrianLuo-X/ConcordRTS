// Included by RuntimeShell.cpp after the shared impact timeline helpers.
void setRuntimeInfantryDeathTimeline(
    RuntimeImpactEffect& effect, int start, int count, int delay, int holdTicks
) {
    effect.animationFrameCount = start + count;
    effect.animationFrameDelayTicks = delay;
    effect.animationStartFrame = start;
    effect.animationLoopStartFrame = start;
    effect.animationLoopEndFrame = start + count;
    effect.animationEndFrame = start + count;
    effect.animationLoopCount = 1;
    effect.animationSequenceSteps = count;
    effect.frameIndex = start;
    effect.totalTicks = count * delay + holdTicks;
    effect.remainingTicks = effect.totalTicks;
}

bool spawnRuntimeInfantryDeathSequence(
    RuntimeWindowState& state,
    const WorldObjectInstance& object,
    const UnitType& unit,
    const RuntimeProjectileEffect& projectile
) {
    const WarheadType* warhead = findRuntimeWarheadType(state.rules, projectile.warheadId);
    if (warhead != nullptr && warhead->temporal) {
        return false;
    }
    const int infDeath = warhead == nullptr ? 0 : warhead->infDeath;
    const std::string& special = state.rules.getInfantryDeathAnim(infDeath);
    if (!unit.notHuman && !special.empty() && state.art.findArtType(special) != nullptr) {
        return false;
    }
    const bool inWater = !object.onBridge && !object.airborne
        && runtimeProductionCellIsOpenSurfaceWater(state, {object.x, object.y});
    const auto sequence = runtimeInfantryDeathSequence(
        state.art, unit, infDeath, object.facing, inWater,
        runtimeArcticTheater(state.theaterLoaded ? state.theater.getTileExtension() : state.report.map.theater)
    );
    if (!sequence.has_value()) {
        return false;
    }
    RuntimeImpactEffect effect;
    effect.projectileActorId = projectile.actorId;
    effect.attackerObjectId = projectile.attackerObjectId;
    effect.targetObjectId = object.instanceId;
    effect.owner = object.owner;
    effect.cell = {object.x, object.y};
    effect.level = object.level;
    effect.hasWorldPosition = true;
    const WorldObjectCellOffset offset = worldObjectSubCellOffset(object);
    effect.worldX = runtimeObjectRenderX(object) + offset.x;
    effect.worldY = runtimeObjectRenderY(object) + offset.y;
    effect.worldLevel = runtimeDrawLevelForObject(object);
    effect.facing = object.facing;
    effect.death = true;
    effect.lifecycleState = "InfantryDeath";
    effect.renderIntent = "infantryDeath";
    effect.animId = sequence->artId;
    effect.warheadId = projectile.warheadId;
    const int tickHz = std::clamp(state.config.fixedTickHz, 1, 240);
    setRuntimeInfantryDeathTimeline(effect, sequence->startFrame, sequence->frameCount,
        std::max(1, tickHz / 15), tickHz);
    state.impactEffects.push_back(std::move(effect));
    return true;
}
