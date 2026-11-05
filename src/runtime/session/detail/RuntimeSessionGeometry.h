#pragma once
#include "render/core/RuntimeMapRenderer.h"
#include <memory>
namespace concord::runtime::session_detail {
// Narrow synchronous presentation queries. All inputs/results are the
// existing standard values. No Session/Root callback or retained world index.
class RuntimeSessionAirPresentationBatch {
public:
    virtual ~RuntimeSessionAirPresentationBatch() = default;
    virtual bool capture(const WorldObjectInstance&,RuntimeParticleAirbornePresentation&,std::string* = nullptr) = 0;
    virtual RuntimeAircraftAirbornePresentationBatch::Statistics statistics() const = 0;
};
class RuntimeSessionGeometry {
public:
    virtual ~RuntimeSessionGeometry() = default;
    virtual bool available() const noexcept = 0;
    virtual bool captureRuntimeAircraftAirbornePresentation(
    const GameWorld& world, const WorldObjectInstance& aircraft,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    RuntimeParticleAirbornePresentation& presentation, std::string* error = nullptr
) = 0;
    virtual bool pickRuntimeMapSurfaceCell(
    const GameWorld& world,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,
    const RuntimeInputPixelRect& viewport,
    RuntimeInputPixelPoint screenPoint,
    RuntimeMapRenderCache& cache,
    RuntimeMapSurfaceCell& cell
) = 0;
    virtual bool resolveRuntimeAirImpactBodyCenterOffset(
    const GameWorld& world, const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    double hitX, double hitY, double hitLevel,
    int& screenX, int& screenY, int& fixedX, int& fixedY,
    RuntimeParticleAirbornePresentation& presentation, int& spriteOffsetY
) = 0;
    virtual bool resolveRuntimeInfantryDeathCanvasPivotOffsets(
    const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache,
    int& screenX, int& screenY, int& fixedX, int& fixedY
) = 0;
    virtual bool resolveRuntimeInfantryWeaponMuzzlePoint(const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, RuntimeInputPixelPoint& result) = 0;
    virtual bool resolveRuntimeMarineWeaponMuzzlePoint(const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, RuntimeInputPixelPoint& result) = 0;
    virtual bool resolveRuntimeObjectShpSpriteBounds(
    const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& center,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache,
    RuntimeInputPixelRect& spriteRect,
    bool stableGlobalCanvas = false
) = 0;
    virtual bool resolveRuntimeStructureWeaponMuzzlePoint(const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    int forwardLeptons, int lateralLeptons, int heightLeptons,
    int firingFacing, bool secondary, RuntimeInputPixelPoint& result) = 0;
    virtual bool resolveRuntimeVoxelWeaponMuzzlePoint(const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& bodyCenter, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    int forwardLeptons, int lateralLeptons, int heightLeptons,
    int firingFacing, RuntimeInputPixelPoint& point, bool useBarrelEndpoint = true) = 0;
    virtual double runtimeKirovClearanceTargetLevel(
    const GameWorld& world, const WorldObjectInstance& aircraft,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    double directionX, double directionY, double approachSpeed = -1.0
) = 0;
    virtual RuntimeInputPixelPoint runtimeObjectBodyAnchorForRender(
    const WorldObjectInstance& object,
    const RuntimeInputPixelRect& viewport,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera
) = 0;
    virtual RuntimeObjectWorldRenderPlacement runtimeObjectWorldRenderPlacement(
    const WorldObjectInstance& object, const GameWorld& world, const RuntimeInputPixelRect& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache
) = 0;
    virtual RuntimeInputPixelPoint runtimeProjectileActorScreenPoint(const RuntimeProjectileRenderActor& actor,
    const RuntimeInputPixelRect& viewport, const RuntimeMapCamera& camera) = 0;
    virtual RuntimeInputPixelPoint runtimeStructureTurretAnimScreenOffsetForRender(
    const BuildingType& buildingType,
    const std::string& structureType,
    int footprintWidth,
    int footprintHeight,
    const RuntimeMapCamera& camera
) = 0;
    virtual std::unique_ptr<RuntimeSessionAirPresentationBatch> beginAircraftBatch(const GameWorld&,const RuntimeMapRenderContext&,RuntimeMapRenderCache&) = 0;
};
class RuntimeSessionPrewarmMerge {
public:
    virtual ~RuntimeSessionPrewarmMerge() = default;
    virtual void merge(RuntimeMapRenderCache&,int tick) = 0;
};
}
