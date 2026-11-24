#pragma once
#include "world/values/RuntimeCellPoint.h"
#include "simulation/effects/RuntimeParticlePresentationValues.h"
#include <string>
namespace concord::runtime::session_detail {
struct RuntimeImpactEffect {
    int projectileActorId = -1;
    int attackerObjectId = -1;
    int targetObjectId = -1;
    bool targetIsStructure = false;
    std::string owner;
    RuntimeCellPoint cell;
    int level = 0;
    bool hasWorldPosition = false;
    double worldX = 0.0;
    double worldY = 0.0;
    double worldLevel = 0.0;
    int screenOffsetX = 0;
    int screenOffsetY = 0;
    int nativeFixedX = 0;
    int nativeFixedY = 0;
    bool bodyCenterAnchored = false;
    RuntimeParticleAirbornePresentation bodyCenterPresentation{};
    int bodyCenterSpriteOffsetY = 0;
    int totalTicks = 8;
    int remainingTicks = 8;
    int radius = 10;
    int frameIndex = -1;
    int facing = -1;
    // Last resolved owner position for a moving passenger's muzzle. Together
    // with worldX/Y/Level it preserves the authored offset across snapshots.
    double muzzleAnchorX = 0.0;
    double muzzleAnchorY = 0.0;
    double muzzleAnchorLevel = 0.0;
    int muzzleFacing = -1;
    int animationFrameCount = 0;
    int animationFrameDelayTicks = 1;
    int animationStartFrame = 0;
    int animationLoopStartFrame = 0;
    int animationLoopEndFrame = 0;
    int animationLoopCount = 1;
    int animationEndFrame = 0;
    int animationSequenceSteps = 0;
    int animationMidpointWidth = 0;
    int animationMidpointHeight = 0;
    int lastSmudgeMidpointStep = -1;
    // Source ownership is independent of the victim's corpse remap color.
    std::string deferredParticleAnimId;
    std::string deferredParticleOwner;
    int deferredParticleTicks = -1;
    bool death = false;
    std::string lifecycleState = "Impact";
    std::string renderIntent = "impact";
    std::string weaponId;
    std::string weaponSlot;
    std::string projectileId;
    std::string projectileImageId;
    std::string animId;
    std::string warheadId;
    std::string warheadImpactIntentId;
    std::string trailerIntentId;
    std::string particleId;
    int damage = 0;
    double cellSpread = 0.0;
    bool bright = false;
};

}
