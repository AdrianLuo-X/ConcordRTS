#pragma once

#include "render/geometry/RuntimeObjectWorldRenderPlacement.h"
#include "render/geometry/RuntimeSpriteHitSpanQueries.h"
#include "render/geometry/RuntimeBuildingVisibleHitSpans.h"
#include "render/geometry/RuntimeStructureSpriteQuery.h"
#include "render/geometry/RuntimeStructureTurretScreenOffset.h"
#include "render/geometry/RuntimeMapSurfacePick.h"
#include "render/geometry/RuntimeInfantryMuzzleQuery.h"
#include "render/geometry/RuntimeMarineMuzzleQuery.h"
#include "render/geometry/RuntimeStructureMuzzleQuery.h"
#include "render/geometry/RuntimeVoxelMuzzleQuery.h"
#include "render/geometry/RuntimeSpawnedMissileBoundsQuery.h"
#include "render/geometry/RuntimeObjectShpBoundsQuery.h"
#include "render/geometry/RuntimeMobileSpriteBoundsQuery.h"
#include "render/geometry/RuntimeProjectileScreenPoint.h"
#include "render/geometry/RuntimeObjectBodyAnchor.h"
#include <cstdint>
#include <limits>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Private value snapshot for the existing Win32 renderer/diagnostics only.
// No handles, algorithms, callbacks or retained references cross this boundary.
struct RuntimeNativeRenderPlacementWin32 {
    POINT center{};
    POINT sortPoint{};
    RECT bodyViewport{};
    bool airborneForeground = false;
    int factoryForegroundHostId = -1;
};

static_assert(sizeof(LONG)==sizeof(std::int32_t) && std::numeric_limits<LONG>::digits==31);
static_assert(sizeof(RuntimeNativeRenderPlacementWin32)==sizeof(RuntimeObjectWorldRenderPlacement));
static_assert(alignof(RuntimeNativeRenderPlacementWin32)==alignof(RuntimeObjectWorldRenderPlacement));

inline RuntimeInputPixelPoint runtimeRenderPlacementPixelPoint(const POINT& p) noexcept {
    return {static_cast<std::int32_t>(p.x),static_cast<std::int32_t>(p.y)};
}
inline RuntimeInputPixelRect runtimeRenderPlacementPixelRect(const RECT& r) noexcept {
    return {static_cast<std::int32_t>(r.left),static_cast<std::int32_t>(r.top),
        static_cast<std::int32_t>(r.right),static_cast<std::int32_t>(r.bottom)};
}
inline bool runtimeSpriteHitSpansContainWin32(
    const std::vector<RuntimeSpriteHitSpan>& spans, POINT point) {
    return runtimeSpriteHitSpansContain(spans,runtimeRenderPlacementPixelPoint(point));
}
inline RECT runtimeRenderPlacementNativeRect(RuntimeInputPixelRect r) noexcept {
    return {r.left,r.top,r.right,r.bottom};
}
inline POINT runtimeRenderPlacementNativePoint(RuntimeInputPixelPoint p) noexcept {
    return {p.x,p.y};
}
inline RuntimeNativeRenderPlacementWin32 runtimeRenderPlacementNative(
    const RuntimeObjectWorldRenderPlacement& p) noexcept {
    return {{p.center.x,p.center.y},{p.sortPoint.x,p.sortPoint.y},
        {p.bodyViewport.left,p.bodyViewport.top,p.bodyViewport.right,p.bodyViewport.bottom},
        p.airborneForeground,p.factoryForegroundHostId};
}

inline RuntimeObjectWorldRenderPlacement runtimeObjectWorldRenderPlacementWin32(
    const WorldObjectInstance& object, const GameWorld& world, const RECT& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache) {
    return runtimeObjectWorldRenderPlacement(object,world,
        runtimeRenderPlacementPixelRect(viewport),context,camera,cache);
}

inline POINT runtimeObjectBodyAnchorForRenderWin32(
    const WorldObjectInstance& object, const RECT& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera) {
    return runtimeRenderPlacementNativePoint(runtimeObjectBodyAnchorForRender(
        object,runtimeRenderPlacementPixelRect(viewport),context,camera));
}

inline POINT runtimeProjectileActorScreenPointWin32(
    const RuntimeProjectileRenderActor& object, const RECT& viewport,
    const RuntimeMapCamera& camera) {
    return runtimeRenderPlacementNativePoint(runtimeProjectileActorScreenPoint(
        object,runtimeRenderPlacementPixelRect(viewport),camera));
}

inline bool resolveRuntimeBuildingVisibleHitSpansWin32(
    const GameWorld& world, const RECT& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache,
    std::unordered_map<int,std::vector<RuntimeSpriteHitSpan>>& visibleSpans) {
    return resolveRuntimeBuildingVisibleHitSpans(world,
        runtimeRenderPlacementPixelRect(viewport),context,camera,cache,visibleSpans);
}

inline bool resolveRuntimeStructureSpriteGeometryWin32(
    const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RuntimeStructureSpriteGeometry& geometry) {
    return resolveRuntimeStructureSpriteGeometry(object,
        runtimeRenderPlacementPixelPoint(center),context,camera,cache,geometry);
}

inline POINT runtimeStructureTurretAnimScreenOffsetForRenderWin32(
    const BuildingType& buildingType, const std::string& structureType,
    int footprintWidth, int footprintHeight, const RuntimeMapCamera& camera) {
    return runtimeRenderPlacementNativePoint(
        runtimeStructureTurretAnimScreenOffsetForRender(buildingType,
            structureType,footprintWidth,footprintHeight,camera));
}

// Existing Win32-only diagnostic facade; original SDK ABI and implementation unchanged.
POINT runtimeStructureBarrelRecoilScreenOffsetForRender(
    const WorldObjectInstance& object,
    const BuildingType& buildingType,
    int animationTick
);

inline bool pickRuntimeMapSurfaceCellWin32(
    const GameWorld& world, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, const RECT& viewport, POINT screenPoint,
    RuntimeMapRenderCache& cache, POINT& cell) {
    RuntimeMapSurfaceCell pickedCell{};
    const bool picked = pickRuntimeMapSurfaceCell(world,context,camera,
        runtimeRenderPlacementPixelRect(viewport),
        runtimeRenderPlacementPixelPoint(screenPoint),cache,pickedCell);
    if (picked) cell = {pickedCell.x,pickedCell.y};
    return picked;
}

inline bool resolveRuntimeInfantryWeaponMuzzlePointWin32(
    const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, POINT& result) {
    RuntimeInputPixelPoint resolvedPoint{};
    const bool resolved = resolveRuntimeInfantryWeaponMuzzlePoint(object,
        runtimeRenderPlacementPixelPoint(center),context,camera,cache,resolvedPoint);
    if (resolved) result = {resolvedPoint.x,resolvedPoint.y};
    return resolved;
}

inline bool resolveRuntimeMarineWeaponMuzzlePointWin32(
    const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, POINT& result) {
    RuntimeInputPixelPoint resolvedPoint{};
    const bool resolved = resolveRuntimeMarineWeaponMuzzlePoint(object,
        runtimeRenderPlacementPixelPoint(center),context,camera,cache,resolvedPoint);
    if (resolved) result = {resolvedPoint.x,resolvedPoint.y};
    return resolved;
}

inline bool resolveRuntimeStructureWeaponMuzzlePointWin32(
    const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, int forwardLeptons, int lateralLeptons,
    int heightLeptons, int firingFacing, bool secondary, POINT& result) {
    RuntimeInputPixelPoint resolvedPoint{};
    const bool resolved = resolveRuntimeStructureWeaponMuzzlePoint(object,
        runtimeRenderPlacementPixelPoint(center),context,camera,cache,
        forwardLeptons,lateralLeptons,heightLeptons,firingFacing,secondary,resolvedPoint);
    if (resolved) result = {resolvedPoint.x,resolvedPoint.y};
    return resolved;
}

inline bool resolveRuntimeVoxelWeaponMuzzlePointWin32(
    const WorldObjectInstance& object, const POINT& bodyCenter,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, int forwardLeptons, int lateralLeptons,
    int heightLeptons, int firingFacing, POINT& point, bool useBarrelEndpoint = true) {
    RuntimeInputPixelPoint resolvedPoint{};
    const bool resolved = resolveRuntimeVoxelWeaponMuzzlePoint(object,
        runtimeRenderPlacementPixelPoint(bodyCenter),context,camera,cache,
        forwardLeptons,lateralLeptons,heightLeptons,firingFacing,resolvedPoint,useBarrelEndpoint);
    if (resolved) point = {resolvedPoint.x,resolvedPoint.y};
    return resolved;
}

inline bool resolveRuntimeSpawnedMissileSpriteBoundsWin32(
    const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RECT& bounds) {
    RuntimeInputPixelRect resolvedBounds{};
    const bool resolved = resolveRuntimeSpawnedMissileSpriteBounds(object,
        runtimeRenderPlacementPixelPoint(center),context,camera,cache,resolvedBounds);
    if (resolved) bounds = runtimeRenderPlacementNativeRect(resolvedBounds);
    return resolved;
}

inline bool resolveRuntimeObjectShpSpriteBoundsWin32(
    const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RECT& spriteRect, bool stableGlobalCanvas = false) {
    RuntimeInputPixelRect resolvedBounds{};
    const bool resolved = resolveRuntimeObjectShpSpriteBounds(object,
        runtimeRenderPlacementPixelPoint(center),context,camera,cache,resolvedBounds,stableGlobalCanvas);
    spriteRect = runtimeRenderPlacementNativeRect(resolvedBounds);
    return resolved;
}

inline bool resolveRuntimeMobileObjectSpriteBoundsWin32(
    const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RECT& spriteRect) {
    RuntimeInputPixelRect resolvedBounds{};
    const bool resolved = resolveRuntimeMobileObjectSpriteBounds(object,
        runtimeRenderPlacementPixelPoint(center),context,camera,cache,resolvedBounds);
    spriteRect = runtimeRenderPlacementNativeRect(resolvedBounds);
    return resolved;
}

#include "render/geometry/RuntimeMobileSpriteBoundsProbe.h"

// Original private native pixel oracle; implementation and GDI lifetimes stay with its owner.
bool verifyRuntimeMobileObjectSpriteBounds(
    const WorldObjectInstance& object, const POINT& center,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RuntimeMobileSpriteBoundsProbe& probe
);
