#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/art/ArtDatabase.h"
#include "world/model/GameWorld.h"

namespace concord::render {

using RuntimeSequenceCaseFold = std::string (*)(std::string);

static inline void addRuntimeSequenceCandidate(
    std::vector<std::string>& candidates,
    const std::string& name, RuntimeSequenceCaseFold toLowerAscii
) {
    if (name.empty()) {
        return;
    }
    const std::string token = toLowerAscii(name);
    for (const std::string& candidate : candidates) {
        if (toLowerAscii(candidate) == token) {
            return;
        }
    }
    candidates.push_back(name);
}

static inline bool runtimeMissionHasToken(
    const std::string& mission,
    const std::string& token
) {
    return mission.find(token) != std::string::npos;
}

static inline bool runtimeObjectShouldUseMovingSequence(
    const WorldObjectInstance& object
) {
    return object.hasRenderPosition && object.renderMoveActive;
}

static inline std::vector<std::string> runtimeSequenceCandidatesForObjectState(
    const WorldObjectInstance& object, bool swimming, RuntimeSequenceCaseFold toLowerAscii
) {
    const std::string mission = toLowerAscii(object.mission);
    std::vector<std::string> candidates;
    const auto append = [&](const std::string& name) {
        addRuntimeSequenceCandidate(candidates, name, toLowerAscii);
    };

    if (object.airborne && mission == "paradropdrop") {
        append("Paradrop");
    }
    const bool undeploying = runtimeMissionHasToken(mission, "undeploy");
    const bool deployedFire =
        runtimeMissionHasToken(mission, "deployedfire");
    const bool deployFire =
        !deployedFire && runtimeMissionHasToken(mission, "deployfire");
    const bool deployedIdle =
        runtimeMissionHasToken(mission, "deployedidle")
        || runtimeMissionHasToken(mission, "deployidle")
        || mission == "deployed"
        || (object.deployed
            && !undeploying
            && !deployedFire
            && !deployFire
            && !runtimeMissionHasToken(mission, "deploying"));
    const bool deploying =
        !undeploying
        && !deployedFire
        && !deployFire
        && !deployedIdle
        && runtimeMissionHasToken(mission, "deploy");
    const bool secondaryFire =
        runtimeMissionHasToken(mission, "secondary")
        || runtimeMissionHasToken(mission, "altfire");
    const bool firing =
        deployedFire
        || deployFire
        || secondaryFire
        || runtimeMissionHasToken(mission, "attack")
        || runtimeMissionHasToken(mission, "fire");
    const bool moving = runtimeObjectShouldUseMovingSequence(object);
    const bool shoveling = object.kind == WorldObjectKind::Infantry
        && !moving && mission == "harvestload";
    const bool carrying = object.kind == WorldObjectKind::Infantry
        && moving && mission == "return";
    if (swimming) {
        // Pursuit still uses Swim until the body stops to attack. Wet poses
        // also take precedence over a land prone/deployed state.
        if (moving) {
            append("Swim");
        } else if (firing) {
            append("WetAttack");
        } else if (runtimeMissionHasToken(mission, "idle1")) {
            append("WetIdle1");
        } else if (runtimeMissionHasToken(mission, "idle2")) {
            append("WetIdle2");
        }
        append("Tread");
    }
    if (object.airborne) {
        if (firing) {
            append("FireFly");
        } else if (moving) {
            append("Fly");
        } else {
            append("Hover");
        }
    }

    if (undeploying) {
        append("Undeploy");
    } else if (deploying) {
        append("Deploy");
    } else if (deployedFire) {
        append("DeployedFire");
        append("DeployFire");
        append("Fire");
        append("SecondaryFire");
        append("Deployed");
        append("FireUp");
    } else if (deployFire) {
        append("DeployFire");
        append("DeployedFire");
        append("Fire");
        append("SecondaryFire");
        append("Deployed");
        append("FireUp");
    } else if (deployedIdle) {
        append("DeployedIdle");
        append("DeployIdle");
        append("Deployed");
        append("Guard");
        append("Ready");
    } else if (secondaryFire) {
        if (object.prone) {
            append("SecondaryProne");
            append("FireProne");
        }
        append("SecondaryFire");
        append("Fire");
        append("FireUp");
    } else if (firing) {
        if (object.prone) {
            append("FireProne");
            append("SecondaryProne");
        }
        append("Fire");
        append("FireUp");
        append("SecondaryFire");
    }

    if (shoveling && !firing && !deploying && !undeploying) {
        append("Shovel");
    }
    if (moving && !firing && !deploying && !undeploying) {
        if (object.kind == WorldObjectKind::Infantry && !object.airborne
            && runtimeMissionHasToken(mission, "panic")) append("Panic");
        if (carrying) append("Carry");
        append(object.prone ? "Crawl" : "Walk");
        append(object.prone ? "Walk" : "Crawl");
    }

    if (!moving && !firing && !deploying && !undeploying && !deployedIdle) {
        if (object.prone) {
            append("Prone");
            append("Crawl");
            append("Ready");
        } else if (runtimeMissionHasToken(mission, "cheer")) {
            append("Cheer");
            append("Idle1");
        } else if (runtimeMissionHasToken(mission, "idle1")) {
            append("Idle1");
            append("Idle");
            append("Ready");
        } else if (runtimeMissionHasToken(mission, "idle2")) {
            append("Idle2");
            append("Idle");
            append("Ready");
        } else if (runtimeMissionHasToken(mission, "guard")) {
            append("Guard");
            append("Ready");
        } else {
            append("Ready");
            append("Guard");
        }
    }

    append("Ready");
    append("Guard");
    return candidates;
}

static inline const ArtType::SequenceFrame* findRuntimeSequenceFrame(
    const ArtType& artType,
    const std::vector<std::string>& candidates, RuntimeSequenceCaseFold toLowerAscii
) {
    for (const std::string& candidate : candidates) {
        const std::string token = toLowerAscii(candidate);
        for (const ArtType::SequenceFrame& frame : artType.sequenceFrames) {
            if (frame.present && toLowerAscii(frame.name) == token) {
                return &frame;
            }
        }
    }
    return nullptr;
}

static inline int runtimeInfantrySequenceDelayTicks(const ArtType* artType, int fixedTickHz) {
    // Preserve the existing nominal sequence cadence pending a separate
    // InfantryClass sequence-rate comparison. AnimType::Rate is independent.
    const int legacyTicks = !artType || artType->rate <= 0 ? 6
        : std::clamp((900 + artType->rate / 2) / artType->rate, 1, 36);
    return std::max(1, static_cast<int>((static_cast<long long>(legacyTicks)
        * std::clamp(fixedTickHz, 1, 1000) + 29) / 30));
}

template <class UnitAnimationTick>
static inline int runtimeSequenceFramePhase(
    const WorldObjectInstance& object,
    int animationTick, int fixedTickHz,
    const ArtType& artType,
    const ArtType::SequenceFrame& frame,
    bool reversedDeployFallback, RuntimeSequenceCaseFold toLowerAscii,
    UnitAnimationTick unitAnimationTick
) {
    const int span = std::max(1, frame.frameCount);
    if (span <= 1) {
        return 0;
    }

    const std::string sequenceName = toLowerAscii(frame.name);
    if (runtimeObjectShouldUseMovingSequence(object)
        && (runtimeMissionHasToken(sequenceName, "walk")
            || runtimeMissionHasToken(sequenceName, "crawl")
            || sequenceName == "fly"
            || sequenceName == "swim"
            || sequenceName == "panic"
            || sequenceName == "carry")) {
        return worldObjectMovementAnimationPhase(object, span);
    }
    if ((runtimeMissionHasToken(sequenceName, "deploy")
            || runtimeMissionHasToken(sequenceName, "undeploy"))
        && object.buildAnimTicksTotal > 0) {
        const int total = std::max(1, object.buildAnimTicksTotal);
        const int done =
            std::clamp(total - object.buildAnimTicksRemaining, 0, total);
        int phase = (done * (span - 1)) / total;
        if (reversedDeployFallback) {
            phase = (span - 1) - phase;
        }
        return std::clamp(phase, 0, span - 1);
    }

    const int framePhaseSeed = std::abs(
        (object.instanceId * 31)
        + (object.x * 7)
        + (object.y * 11)
        + (object.subCell * 13)
        + (object.facing * 3)
    );
    int step = animationTick / runtimeInfantrySequenceDelayTicks(&artType, fixedTickHz);
    if (sequenceName == "shovel" || sequenceName == "tread" || sequenceName == "swim"
        || sequenceName == "wetattack" || sequenceName == "wetidle1"
        || sequenceName == "wetidle2") {
        const std::int64_t tick = unitAnimationTick();
        const int delay = runtimeInfantrySequenceDelayTicks(&artType, fixedTickHz);
        step = static_cast<int>((tick / delay) % span);
    }
    return (step + framePhaseSeed) % span;
}

struct RuntimeObjectSequenceFrameResult {
    bool matched = false;
    int frameIndex = 0;
    std::string sequenceName;
};

struct RuntimeObjectSequenceTiming {
    int animationTick;
    int fixedTickHz;
    const std::unordered_map<int, int>* playerCheerUntilTick;
};

template <class Entry>
struct RuntimeSequenceFrameMetadataCacheView {
    std::unordered_map<std::string, Entry>& entries;
    int& hits;
    int& misses;
};

template <class Entry, class SwimmingQuery, class UnitAnimationTick>
static inline RuntimeObjectSequenceFrameResult selectRuntimeObjectSequenceFrame(
    const WorldObjectInstance& object,
    const ArtType* artType,
    int frameCount,
    const RuntimeObjectSequenceTiming& timing,
    RuntimeSequenceFrameMetadataCacheView<Entry>* cache,
    RuntimeSequenceCaseFold toLowerAscii,
    int (*facingBucketForDirectionalShp)(int, int),
    const SwimmingQuery& infantrySwimming,
    const UnitAnimationTick& unitAnimationTick
) {
    RuntimeObjectSequenceFrameResult result;
    if (artType == nullptr
        || artType->sequenceFrames.empty()
        || frameCount <= 1) {
        return result;
    }

    // Cheer changes only the displayed idle sequence, never orders or world state.
    if (timing.playerCheerUntilTick && object.kind == WorldObjectKind::Infantry
        && !object.airborne && !object.deployed && !object.prone
        && !runtimeObjectShouldUseMovingSequence(object)
        && (toLowerAscii(object.mission) == "guard" || toLowerAscii(object.mission) == "idle")) {
        const auto cheer = timing.playerCheerUntilTick->find(object.instanceId);
        if (cheer != timing.playerCheerUntilTick->end() && cheer->second > timing.animationTick) {
            WorldObjectInstance presentation = object;
            presentation.mission = "Cheer";
            return selectRuntimeObjectSequenceFrame(presentation, artType, frameCount, timing,
                cache, toLowerAscii, facingBucketForDirectionalShp, infantrySwimming, unitAnimationTick);
        }
    }
    int cacheFacingCount =
        artType->facings > 0 ? artType->facings : 8;
    cacheFacingCount = std::clamp(cacheFacingCount, 1, 32);
    const int cacheFacingBucket =
        cacheFacingCount > 1
            ? facingBucketForDirectionalShp(
                object.facing,
                cacheFacingCount
            )
            : 0;
    const std::string mission = toLowerAscii(object.mission);
    const bool movingSequence = runtimeObjectShouldUseMovingSequence(object);
    const bool swimming = infantrySwimming(object);
    const auto animationTick = [&] { return unitAnimationTick(object); };
    const std::string cacheKey =
        artType->id
        + "|kind=" + toString(object.kind)
        + "|mission=" + mission
        + "|airborne=" + (object.airborne ? "1" : "0")
        + "|render=" + (object.hasRenderPosition ? "1" : "0")
        + "|moving=" + (movingSequence ? "1" : "0")
        + "|swimming=" + (swimming ? "1" : "0")
        + "|prone=" + (object.prone ? "1" : "0")
        + "|deployed=" + (object.deployed ? "1" : "0")
        + "|facing=" + std::to_string(cacheFacingBucket)
        + "|frames=" + std::to_string(frameCount);
    if (cache != nullptr) {
        const auto found = cache->entries.find(cacheKey);
        if (found != cache->entries.end()) {
            ++cache->hits;
            const Entry& metadata =
                found->second;
            if (!metadata.matched) {
                return result;
            }
            const ArtType::SequenceFrame frame{
                metadata.sequenceName,
                true,
                metadata.startFrame,
                metadata.framesPerFacing,
                metadata.facingStride
            };
            const int phase = runtimeSequenceFramePhase(
                object, timing.animationTick, timing.fixedTickHz, *artType, frame,
                metadata.reversedDeployFallback, toLowerAscii, animationTick
            );
            int frameIndex =
                metadata.startFrame
                + (metadata.facingBucket * metadata.facingStride)
                + std::clamp(phase, 0, metadata.framesPerFacing - 1);
            if (frameIndex < 0 || frameIndex >= frameCount) {
                frameIndex %= frameCount;
                if (frameIndex < 0) {
                    frameIndex += frameCount;
                }
            }
            result.matched = true;
            result.frameIndex = frameIndex;
            result.sequenceName = metadata.sequenceName;
            return result;
        }
        ++cache->misses;
    }

    const std::vector<std::string> candidates =
        runtimeSequenceCandidatesForObjectState(object, swimming, toLowerAscii);
    const ArtType::SequenceFrame* sequence =
        findRuntimeSequenceFrame(*artType, candidates, toLowerAscii);
    if (sequence == nullptr) {
        if (cache != nullptr) {
            cache->entries.emplace(
                cacheKey,
                Entry{}
            );
        }
        return result;
    }

    const bool reversedDeployFallback =
        runtimeMissionHasToken(mission, "undeploy")
        && toLowerAscii(sequence->name) == "deploy";
    const int framesPerFacing = std::clamp(
        sequence->frameCount > 0 ? sequence->frameCount : 1,
        1,
        std::max(1, frameCount)
    );
    const int facingStride = std::max(
        framesPerFacing,
        sequence->facingMultiplier > 0
            ? sequence->facingMultiplier
            : framesPerFacing
    );
    int facingCount =
        sequence->facingMultiplier > 0
            ? (artType->facings > 0 ? artType->facings : 8)
            : 1;
    facingCount = std::clamp(facingCount, 1, 32);
    const int facingBucket =
        facingCount > 1
            ? facingBucketForDirectionalShp(object.facing, facingCount)
            : 0;
    const int phase = runtimeSequenceFramePhase(
        object, timing.animationTick, timing.fixedTickHz, *artType, *sequence,
        reversedDeployFallback, toLowerAscii, animationTick
    );
    int frameIndex =
        sequence->startFrame
        + (facingBucket * facingStride)
        + std::clamp(phase, 0, framesPerFacing - 1);
    if (frameIndex < 0 || frameIndex >= frameCount) {
        frameIndex %= frameCount;
        if (frameIndex < 0) {
            frameIndex += frameCount;
        }
    }

    result.matched = true;
    result.frameIndex = frameIndex;
    result.sequenceName = sequence->name;
    if (cache != nullptr) {
        Entry metadata;
        metadata.matched = true;
        metadata.startFrame = sequence->startFrame;
        metadata.framesPerFacing = framesPerFacing;
        metadata.facingStride = facingStride;
        metadata.facingBucket = facingBucket;
        metadata.reversedDeployFallback = reversedDeployFallback;
        metadata.sequenceName = sequence->name;
        cache->entries.emplace(cacheKey, metadata);
    }
    return result;
}

} // namespace concord::render
