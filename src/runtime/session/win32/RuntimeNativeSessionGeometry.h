#pragma once
#include "runtime/session/detail/RuntimeSessionGeometry.h"
#include <memory>
namespace concord::runtime::session_detail {
class RuntimeNativeAirPresentationBatch final : public RuntimeSessionAirPresentationBatch {
public:
    RuntimeNativeAirPresentationBatch(const GameWorld& world,const RuntimeMapRenderContext& context,RuntimeMapRenderCache& cache):batch_(world,context,cache) {}
    bool capture(const WorldObjectInstance& object,RuntimeParticleAirbornePresentation& presentation,std::string* error=nullptr) override {return batch_.capture(object,presentation,error);}
    RuntimeAircraftAirbornePresentationBatch::Statistics statistics() const override {return batch_.statistics();}
private:
    RuntimeAircraftAirbornePresentationBatch batch_;
};
class RuntimeNativeSessionGeometry final : public RuntimeSessionGeometry {
public:
    bool available() const noexcept override {return true;}
    bool captureRuntimeAircraftAirbornePresentation(
    const GameWorld& world, const WorldObjectInstance& aircraft,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    RuntimeParticleAirbornePresentation& presentation, std::string* error) override { return ::captureRuntimeAircraftAirbornePresentation(world,aircraft,context,cache,presentation,error); }
    bool pickRuntimeMapSurfaceCell(
    const GameWorld& world,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,
    const RuntimeInputPixelRect& viewport,
    RuntimeInputPixelPoint screenPoint,
    RuntimeMapRenderCache& cache,
    RuntimeMapSurfaceCell& cell) override { return ::pickRuntimeMapSurfaceCell(world,context,camera,viewport,screenPoint,cache,cell); }
    bool resolveRuntimeAirImpactBodyCenterOffset(
    const GameWorld& world, const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    double hitX, double hitY, double hitLevel,
    int& screenX, int& screenY, int& fixedX, int& fixedY,
    RuntimeParticleAirbornePresentation& presentation, int& spriteOffsetY) override { return ::resolveRuntimeAirImpactBodyCenterOffset(world,object,context,cache,hitX,hitY,hitLevel,screenX,screenY,fixedX,fixedY,presentation,spriteOffsetY); }
    bool resolveRuntimeInfantryDeathCanvasPivotOffsets(
    const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context,
    RuntimeMapRenderCache& cache,
    int& screenX, int& screenY, int& fixedX, int& fixedY) override { return ::resolveRuntimeInfantryDeathCanvasPivotOffsets(object,context,cache,screenX,screenY,fixedX,fixedY); }
    bool resolveRuntimeInfantryWeaponMuzzlePoint(const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, RuntimeInputPixelPoint& result) override { return ::resolveRuntimeInfantryWeaponMuzzlePoint(object,center,context,camera,cache,result); }
    bool resolveRuntimeMarineWeaponMuzzlePoint(const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache, RuntimeInputPixelPoint& result) override { return ::resolveRuntimeMarineWeaponMuzzlePoint(object,center,context,camera,cache,result); }
    bool resolveRuntimeObjectShpSpriteBounds(
    const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& center,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache,
    RuntimeInputPixelRect& spriteRect,
    bool stableGlobalCanvas) override { return ::resolveRuntimeObjectShpSpriteBounds(object,center,context,camera,cache,spriteRect,stableGlobalCanvas); }
    bool resolveRuntimeStructureWeaponMuzzlePoint(const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& center, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    int forwardLeptons, int lateralLeptons, int heightLeptons,
    int firingFacing, bool secondary, RuntimeInputPixelPoint& result) override { return ::resolveRuntimeStructureWeaponMuzzlePoint(object,center,context,camera,cache,forwardLeptons,lateralLeptons,heightLeptons,firingFacing,secondary,result); }
    bool resolveRuntimeVoxelWeaponMuzzlePoint(const WorldObjectInstance& object,
    const RuntimeInputPixelPoint& bodyCenter, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, RuntimeMapRenderCache& cache,
    int forwardLeptons, int lateralLeptons, int heightLeptons,
    int firingFacing, RuntimeInputPixelPoint& point, bool useBarrelEndpoint) override { return ::resolveRuntimeVoxelWeaponMuzzlePoint(object,bodyCenter,context,camera,cache,forwardLeptons,lateralLeptons,heightLeptons,firingFacing,point,useBarrelEndpoint); }
    double runtimeKirovClearanceTargetLevel(
    const GameWorld& world, const WorldObjectInstance& aircraft,
    const RuntimeMapRenderContext& context, RuntimeMapRenderCache& cache,
    double directionX, double directionY, double approachSpeed) override { return ::runtimeKirovClearanceTargetLevel(world,aircraft,context,cache,directionX,directionY,approachSpeed); }
    RuntimeInputPixelPoint runtimeObjectBodyAnchorForRender(
    const WorldObjectInstance& object,
    const RuntimeInputPixelRect& viewport,
    const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera) override { return ::runtimeObjectBodyAnchorForRender(object,viewport,context,camera); }
    RuntimeObjectWorldRenderPlacement runtimeObjectWorldRenderPlacement(
    const WorldObjectInstance& object, const GameWorld& world, const RuntimeInputPixelRect& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache) override { return ::runtimeObjectWorldRenderPlacement(object,world,viewport,context,camera,cache); }
    RuntimeInputPixelPoint runtimeProjectileActorScreenPoint(const RuntimeProjectileRenderActor& actor,
    const RuntimeInputPixelRect& viewport, const RuntimeMapCamera& camera) override { return ::runtimeProjectileActorScreenPoint(actor,viewport,camera); }
    RuntimeInputPixelPoint runtimeStructureTurretAnimScreenOffsetForRender(
    const BuildingType& buildingType,
    const std::string& structureType,
    int footprintWidth,
    int footprintHeight,
    const RuntimeMapCamera& camera) override { return ::runtimeStructureTurretAnimScreenOffsetForRender(buildingType,structureType,footprintWidth,footprintHeight,camera); }
    std::unique_ptr<RuntimeSessionAirPresentationBatch> beginAircraftBatch(const GameWorld& world,const RuntimeMapRenderContext& context,RuntimeMapRenderCache& cache) override {
        return std::make_unique<RuntimeNativeAirPresentationBatch>(world,context,cache);
    }
};
}
