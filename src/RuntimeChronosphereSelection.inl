// Private presentation for the incomplete two-click Chronosphere command.
// Its original SHP loops independently of mouse position and world effects.
bool runtimeChronosphereSelectionVisibleToLocalOwner(
    const RuntimeWindowState& state, const RuntimeImpactEffect& effect
) {
    if (effect.lifecycleState != "ChronosphereSelection") return true;
    const auto owner = toLowerAsciiLocal(trimAsciiLocal(state.economy.owner));
    if (owner.empty() || owner != toLowerAsciiLocal(trimAsciiLocal(effect.owner))) return false;
    const auto* weapon = state.superWeapons.findSidebarSnapshot(effect.owner, effect.weaponId);
    return weapon && weapon->state == SuperWeaponRuntimeState::AwaitingSecondTarget
        && weapon->firstTargetCell.x == effect.cell.x && weapon->firstTargetCell.y == effect.cell.y;
}

void clearRuntimeLocalChronosphereMarker(RuntimeWindowState& state) {
    stopRuntimeChronosphereSelectionAudio(state);
    if (!state.localChronosphereMarker) return;
    state.localChronosphereMarker.reset();
    state.repaintRequested = true;
}

void beginRuntimeLocalChronosphereMarker(
    RuntimeWindowState& state, const std::string& weaponId, RuntimeCellPoint cell
) {
    const auto* rule = state.rules.findSuperWeaponType(weaponId);
    if (!rule || rule->placementAnim.empty()
        || toLowerAsciiLocal(rule->type + " " + rule->action).find("chrono") == std::string::npos)
        return;
    const auto& previous = state.localChronosphereMarker;
    if (previous && previous->animation.owner == state.economy.owner
        && previous->animation.weaponId == weaponId
        && previous->animation.cell.x == cell.x && previous->animation.cell.y == cell.y) return;
    RuntimeLocalChronosphereMarker marker;
    marker.startedTick = state.tick;
    auto& animation = marker.animation;
    animation.owner = state.economy.owner;
    animation.weaponId = weaponId;
    animation.cell = cell;
    animation.level = terrainLevelAtRuntimeCell(state, cell.x, cell.y);
    animation.animId = rule->placementAnim;
    animation.lifecycleState = "LocalChronosphereSelection";
    animation.renderIntent = "superWeaponEffect";
    animation.hasWorldPosition = true;
    animation.worldX = cell.x;
    animation.worldY = cell.y;
    animation.worldLevel = animation.level + 5.0 * 2.449489742783178 / 256.0;
    initializeRuntimeSuperWeaponAnimation(state, animation);
    if (animation.animationFrameCount <= 0) return;
    state.localChronosphereMarker = std::move(marker);
    state.repaintRequested = true;
    startRuntimeChronosphereSelectionAudio(state,
        state.localChronosphereMarker->animation, GetTickCount64());
}

std::optional<RuntimeImpactRenderActor> runtimeLocalChronosphereRenderActor(
    const RuntimeWindowState& state
) {
    if (!state.localChronosphereMarker) return std::nullopt;
    const auto& animation = state.localChronosphereMarker->animation;
    const auto& intent = state.multiplayerIntentRouter.superWeapon();
    const auto* source = state.superWeapons.findSidebarSnapshot(animation.owner, animation.weaponId);
    const bool sourceAvailable = source && (source->state == SuperWeaponRuntimeState::Ready
        || source->state == SuperWeaponRuntimeState::SelectingTarget
        || source->state == SuperWeaponRuntimeState::AwaitingSecondTarget);
    if (!sourceAvailable || !intent || intent->expectedTargetCount != 2
        || intent->targets.empty()
        || toLowerAsciiLocal(intent->superWeaponId) != toLowerAsciiLocal(animation.weaponId)
        || intent->targets.front().cell.x != animation.cell.x
        || intent->targets.front().cell.y != animation.cell.y) {
        state.localChronosphereMarker.reset();
        return std::nullopt;
    }
    // An observer/allied view never inherits another player's private marker.
    // Keep its clock intact if a temporary view switches back to the caster.
    const auto localOwner = toLowerAsciiLocal(trimAsciiLocal(state.economy.owner));
    if (localOwner.empty() || localOwner != toLowerAsciiLocal(trimAsciiLocal(animation.owner)))
        return std::nullopt;
    const int step = std::max(0, state.tick - state.localChronosphereMarker->startedTick)
        / std::max(1, animation.animationFrameDelayTicks);
    const int intro = std::max(0, animation.animationLoopStartFrame - animation.animationStartFrame);
    const int span = std::max(1, animation.animationLoopEndFrame - animation.animationLoopStartFrame);
    auto actor = makeRuntimeImpactRenderActor(animation);
    actor.frameIndex = step < intro ? animation.animationStartFrame + step
        : animation.animationLoopStartFrame + (step - intro) % span;
    // A presentation marker has no finite effect lifetime or damage actor.
    actor.totalTicks = actor.remainingTicks = 1;
    actor.damage = 0;
    return actor;
}

bool advanceRuntimeLocalChronospherePresentation(RuntimeWindowState& state) {
    if (!state.localChronosphereMarker) return false;
    const auto actor = runtimeLocalChronosphereRenderActor(state);
    if (!state.localChronosphereMarker) {
        state.repaintRequested = true;
        return true;
    }
    if (!actor) return false;
    auto& marker = *state.localChronosphereMarker;
    const int step = std::max(0, state.tick - marker.startedTick)
        / std::max(1, marker.animation.animationFrameDelayTicks);
    if (step == marker.lastAnimationStep) return false;
    marker.lastAnimationStep = step;
    state.repaintRequested = true;
    return true;
}
