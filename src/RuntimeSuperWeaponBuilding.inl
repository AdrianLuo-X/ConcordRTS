// Building art is a projection of the saved weapon clock. No building
// mission changes or independent animation timers are required.
std::optional<SpecialMechanismRenderSnapshot> runtimeSuperWeaponBuildingSnapshot(
    const RuntimeWindowState& state, const WorldObjectInstance& object) {
        if (!object.alive || object.kind != WorldObjectKind::Structure) return std::nullopt;
        const auto* building = state.rules.findBuildingType(object.type);
        if (!building || building->superWeapon.empty()) return std::nullopt;
        const auto* art = state.art.findArtType(building->image.empty() ? object.type : building->image);
        if (!art || std::none_of(art->auxiliaryAnimations.begin(), art->auxiliaryAnimations.end(),
                [](const auto& layer) { return layer.key == "SuperAnim"; })) return std::nullopt;
        const auto* weapon = state.superWeapons.findSidebarSnapshot(object.owner, building->superWeapon);
        SpecialMechanismRenderSnapshot pose;
        pose.kind = "superWeaponBuilding";
        pose.objectId = object.instanceId;
        pose.owner = object.owner;
        pose.resourceId = "SuperAnim";
        if (weapon) {
            const auto phase = weapon->state;
            const int chargedTicks = std::max(0, static_cast<int>(std::lround(
                building->chargedAnimTime * 60.0 * std::max(1, state.config.fixedTickHz))));
            const int launchTick = state.superWeapons.activeLaunchTick(object.owner, building->superWeapon);
            if (launchTick >= 0) {
                pose.resourceId = "SuperAnimFour";
                pose.progress = std::max(0, state.tick - launchTick);
            } else if (phase == SuperWeaponRuntimeState::Ready || phase == SuperWeaponRuntimeState::SelectingTarget
                    || phase == SuperWeaponRuntimeState::AwaitingSecondTarget) {
                pose.resourceId = "SuperAnimThree";
            } else if ((phase == SuperWeaponRuntimeState::Charging || phase == SuperWeaponRuntimeState::Suspended)
                    && weapon->remainingTicks <= chargedTicks) {
                pose.resourceId = "SuperAnimTwo";
                // RemainingTicks stops while power is down, freezing this
                // transition at its actual frame and surviving a save/load.
                pose.progress = chargedTicks - weapon->remainingTicks;
            }
        }
        if (pose.resourceId == "SuperAnim" || pose.resourceId == "SuperAnimThree") {
            // Idle/ready art is often an infinite native SHP loop, not a
            // single frame. Use the building's saved powered animation clock.
            const auto layer = std::find_if(art->auxiliaryAnimations.begin(), art->auxiliaryAnimations.end(),
                [&](const auto& entry) { return entry.key == pose.resourceId; });
            const bool poweredAnimation = layer != art->auxiliaryAnimations.end()
                && (layer->powered || layer->poweredEffect);
            const int sourceTick = poweredAnimation && !object.poweredOperational
                && object.powerAnimationPausedAtTick >= 0 ? object.powerAnimationPausedAtTick : state.tick;
            pose.progress = std::max(0, sourceTick
                - (poweredAnimation ? std::max(0, object.powerAnimationPausedTicks) : 0));
        }
        return pose;
}

void appendRuntimeSuperWeaponBuildingSnapshots(RuntimeWindowState& state) {
    for (const auto& object : state.world.getObjects()) {
        if(auto pose=runtimeSuperWeaponBuildingSnapshot(state,object))
            state.specialMechanismRenderSnapshots.push_back(std::move(*pose));
    }
}
