// Keep combat/save endpoints in world coordinates and attach only the visual
// discharge to the same interpolated body pose used by the unit renderer.
POINT runtimeElectricTargetBodyPoint(
    const WorldObjectInstance* target, double savedX, double savedY, double savedLevel,
    const RECT& viewport, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache
) {
    if (target == nullptr || (target->kind != WorldObjectKind::Unit
            && target->kind != WorldObjectKind::Infantry))
        return screenPoint(projectCellXY(savedX, savedY, savedLevel, camera), viewport, camera);
    WorldObjectInstance pose = *target;
    POINT point{};
    if (pose.alive && pose.health > 0) {
        point = runtimeObjectBodyAnchorForRender(pose, viewport, context, camera);
    } else {
        // Project the saved visual height and subcell together. Re-reading a
        // corpse's level/host pose or applying submerged depth again would
        // move the hit; adding separately rounded offsets can also miss by 1px.
        pose.alive = true;
        pose.health = 1;
        pose.renderMoveActive = false;
        pose.mission = "Guard";
        const auto subCell = worldObjectSubCellOffset(pose);
        point = screenPoint(projectCellXY(savedX + subCell.x, savedY + subCell.y,
            savedLevel, camera), viewport, camera);
        point.y -= runtimeAircraftSpriteLiftPixels(pose,
            runtimeObjectLooksAircraftForRender(pose, context));
    }
    return runtimeChronoEffectAnchor(pose, point, context, camera, cache, false);
}
