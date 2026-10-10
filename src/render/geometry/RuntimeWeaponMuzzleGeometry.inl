#include "render/voxel/RuntimeNativeVoxelMuzzleAnchors.inl"

// Project a weapon in the same model units, fixed scale and body pose as the
// voxel that was drawn. World FLH remains the simulation's trajectory origin;
// this is the captured presentation correction applied to that trajectory.
bool resolveRuntimeVoxelWeaponMuzzlePoint(const WorldObjectInstance& object,
    const POINT& bodyCenter, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    int forwardLeptons, int lateralLeptons, int heightLeptons,
    int firingFacing, POINT& point, bool useBarrelEndpoint) {
    if (!context.rules) return false;
    constexpr double modelUnitsPerLepton = 0.18;
    std::array<double, 3> local{forwardLeptons * modelUnitsPerLepton,
        lateralLeptons * modelUnitsPerLepton, heightLeptons * modelUnitsPerLepton};
    RuntimeVoxelCacheEntry* body = nullptr;
    std::string owner = object.type;
    POINT center = bodyCenter;
    bool turret = false;
    double fitScale = 0.0;
    double bodyFitScale = 0.0;
    RuntimeVoxelModelBounds bodyBounds;
    RuntimeVehicleBodyPose bodyPose;
    RuntimeVoxelBodyRotation rotation;
    const auto useModelEndpoint = [&](const std::vector<std::string>& images,
        const std::string& partOwner, double forcedScale, const POINT& pixelOffset) {
        if (!useBarrelEndpoint || forwardLeptons <= 0) return false;
        for (const auto& image : images) {
            // Match the first drawable candidate, including independent part
            // fitting. A later file may exist without being the part on screen.
            auto* part = loadRuntimeVoxelImageFromCache(image, partOwner,
                facingBucketForFacingValue(firingFacing, runtimeVoxelFacingBucketCount),
                context, cache, forcedScale, 0, 0, 0, &rotation);
            if (!part || !part->hasSprite) continue;
            const auto& source = loadRuntimeVoxelSourceFromCache(image, context, cache, false);
            if (!source.loaded) return false;
            const std::string key = context.assetRootPath + "|" + toLowerAscii(image);
            auto [found, inserted] = cache.voxelMuzzleEndpointsByKey.try_emplace(key);
            if (inserted) {
                std::vector<RuntimeVoxelMuzzleEndpoint> ends;
                std::string error;
                std::array<double, 3> native{};
                if (runtimeNativeVoxelMuzzleAnchor(image, source.vxlBytes, source.hvaBytes, native))
                    found->second.push_back(native);
                else if (resolveRuntimeVoxelMuzzleEndpoints(source.vxlBytes, source.firstFrameTransforms, ends, error))
                    for (const auto& end : ends) found->second.push_back(end.modelPoint);
            }
            if (found->second.empty()) return false;
            // Native lateral FLH selects the firing tube of a multi-barrel
            // model. End faces remain separate, including Apocalypse's pair.
            const auto distance = [&](const auto& end) {
                return (end[0] - local[0]) * (end[0] - local[0])
                    + (end[1] - local[1]) * (end[1] - local[1])
                    + (end[2] - local[2]) * (end[2] - local[2]);
            };
            local = *std::min_element(found->second.begin(), found->second.end(),
                [&](const auto& a, const auto& b) { return distance(a) < distance(b); });
            fitScale = part->rasterFitScale;
            owner = partOwner;
            center.x += pixelOffset.x;
            center.y += pixelOffset.y;
            return true;
        }
        return false;
    };
    if (object.kind == WorldObjectKind::Structure) {
        const auto* building = context.rules->findBuildingType(object.type);
        if (!building || !building->turretAnimIsVoxel || building->turretAnim.empty()) return false;
        owner += "|turretanim";
        const bool grand = toLowerAscii(object.type) == "gtgcan" || toLowerAscii(building->turretAnim) == "gtgcantur";
        const auto footprint = runtimeObjectFoundationDimensions(object, context);
        const bool authoredGrandScale = grand && footprint.width >= 2 && footprint.height >= 2;
        body = loadRuntimeVoxelImageFromCache(building->turretAnim, owner,
            facingBucketForFacingValue(firingFacing, runtimeVoxelFacingBucketCount), context, cache,
            authoredGrandScale ? runtimeGrandCannonModelScale() : 0.0);
        if (!body || !body->hasSprite) return false;
        fitScale = body->rasterFitScale;
        const double partScale = authoredGrandScale ? runtimeGrandCannonModelScale()
            : (runtimeVxlSharedPartScaleEnabled() ? fitScale : 0.0);
        const std::string turretOwner = owner;
        const bool barrelEndpoint = useModelEndpoint(
            runtimeStructureTurretBarrelImageCandidates(building->turretAnim, object.type),
            turretOwner + "|barrel", partScale, POINT{});
        if (!barrelEndpoint)
            useModelEndpoint({building->turretAnim}, turretOwner, partScale, POINT{});
        const auto offset = runtimeStructureTurretAnimScreenOffset(*building, object.type,
            footprint.width, footprint.height, camera);
        const auto recoil = runtimeStructureRecoilScreenOffset(object, *building, context.animationTick,
            building->turretTravel + (barrelEndpoint ? building->barrelTravel : 0));
        center.x += offset.x + recoil.x;
        // Structure voxel parts intentionally enter the foundation SHP anchor.
        center.y += camera.tileHeight / 2 + offset.y + recoil.y;
        turret = true;
    } else {
        if (object.kind != WorldObjectKind::Unit || !objectUsesVoxelArt(object, context)) return false;
        body = loadRuntimeVoxelFromCache(object, context, cache);
        if (!body || !body->hasSprite) return false;
        fitScale = bodyFitScale = body->rasterFitScale;
        bodyBounds = body->modelBounds;
        bodyPose = runtimeVehicleBodyPoseForRender(object, context);
        rotation = runtimeVehicleBodyRotation(bodyPose,
            bodyBounds.valid ? bodyBounds.minimum[2] : 0.0);
        turret = runtimeObjectHasTurretForRender(object, context);
        if (!turret && useBarrelEndpoint && forwardLeptons > 0) {
            // Only the explicitly audited stock hull-integrated guns qualify.
            // Keep the body fit/pivot/rotation and cache fingerprint work once.
            const std::string& image = body->metadata.imageId;
            const std::string key = context.assetRootPath + "|native-body|" + toLowerAscii(image);
            auto [found, inserted] = cache.voxelMuzzleEndpointsByKey.try_emplace(key);
            if (inserted) {
                const auto& source = loadRuntimeVoxelSourceFromCache(image, context, cache, false);
                std::array<double, 3> native{};
                if (source.loaded && runtimeNativeVoxelMuzzleAnchor(image,
                        source.vxlBytes, source.hvaBytes, native)) found->second.push_back(native);
            }
            if (!found->second.empty()) local = found->second.front();
        }
        const auto sizing = runtimeVxlRasterSizingForOwner(owner);
        if (!turret && bodyBounds.valid && !sizing.modelOriginPivot) {
            local[0] -= (bodyBounds.minimum[0] + bodyBounds.maximum[0]) * 0.5;
            local[1] -= (bodyBounds.minimum[1] + bodyBounds.maximum[1]) * 0.5;
        }
        if (turret) {
            // The mounting offset is fitted with the body, then each attached
            // part is fitted independently. Do not rescale the mount with BARL.
            POINT mount = runtimeVoxelTurretOffset(object, context, bodyFitScale);
            if (rotation.active()) {
                const double scale = bodyFitScale > 0.0 ? bodyFitScale : 0.50;
                const auto axes = runtimeVxlProjectionAxes();
                const double sum = mount.y / (axes[1] * scale);
                const double difference = mount.x / (axes[0] * scale);
                const auto rotated = rotation.rotate((sum + difference) * 0.5,
                    (sum - difference) * 0.5, 0.0);
                mount = {static_cast<LONG>(std::lround((rotated[0] - rotated[1]) * axes[0] * scale)),
                    static_cast<LONG>(std::lround(((rotated[0] + rotated[1]) * axes[1]
                        - rotated[2] * axes[2]) * scale))};
            }
            center.x += mount.x;
            center.y += mount.y;
            const bool shared = runtimeVxlSharedPartScaleEnabled();
            const bool barrelEndpoint = runtimeDrawSeparateUnitVoxelBarrels()
                && useModelEndpoint(runtimeUnitBarrelImageCandidates(object, context), object.type + "|barrel",
                    shared ? runtimeVoxelPartFitScale(bodyFitScale, runtimeVxlBarrelRelativeScale()) : 0.0,
                    POINT{runtimeVxlPixelOffset("CONCORD_VXL_BARREL_PIXEL_OFFSET_X"),
                        runtimeVxlPixelOffset("CONCORD_VXL_BARREL_PIXEL_OFFSET_Y")});
            if (!barrelEndpoint)
                useModelEndpoint(runtimeUnitTurretImageCandidates(object, context), object.type + "|turret",
                    shared ? runtimeVoxelPartFitScale(bodyFitScale, runtimeVxlTurretRelativeScale()) : 0.0,
                    POINT{runtimeVxlPixelOffset("CONCORD_VXL_TURRET_PIXEL_OFFSET_X"),
                        runtimeVxlPixelOffset("CONCORD_VXL_TURRET_PIXEL_OFFSET_Y")});
        }
    }
    const auto sizing = runtimeVxlRasterSizingForOwner(owner);
    const auto projection = runtimeVxlProjectionParamsForSizing(sizing);
    const auto yaw = [](int facing) {
        return (worldObjectArtFacingBucket(facing, runtimeVoxelFacingBucketCount) - 8)
            * 6.283185307179586 / runtimeVoxelFacingBucketCount;
    };
    const double gunYaw = yaw(firingFacing), hullYaw = yaw(object.facing);
    double x = local[0] * std::cos(gunYaw) - local[1] * std::sin(gunYaw);
    double y = local[0] * std::sin(gunYaw) + local[1] * std::cos(gunYaw);
    double z = local[2];
    if (rotation.active()) {
        const auto rotated = rotation.rotate(x, y, z - rotation.floorZ);
        x = rotated[0]; y = rotated[1]; z = rotated[2] + rotation.floorZ;
    }
    if (bodyPose.active() && !bodyPose.hullLocalRoll) {
        const auto bodySizing = runtimeVxlRasterSizingForOwner(object.type);
        const auto bodyProjection = runtimeVxlProjectionParamsForSizing(bodySizing);
        const double support = runtimeVehicleGroundSupportLift(bodyBounds, rotation, hullYaw,
            bodySizing.modelOriginPivot);
        center.y -= static_cast<LONG>(std::lround(support * bodyFitScale * bodyProjection.heightYScale
            + bodyPose.height * camera.tileHeight));
    }
    point = {center.x + static_cast<LONG>(std::lround((x - y) * projection.screenXScale * fitScale)),
        center.y + static_cast<LONG>(std::lround(((x + y) * projection.groundYScale
            - z * projection.heightYScale) * fitScale))};
    return true;
}
