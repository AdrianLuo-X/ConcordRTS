bool runtimeSuperWeaponBuildingLayerKey(const std::string& key) {
    return key == "SuperAnim" || key == "SuperAnimTwo"
        || key == "SuperAnimThree" || key == "SuperAnimFour";
}

int runtimeSuperWeaponBuildingFrameDelay(const ArtType* art, int tickHz) {
    return runtimeSuperWeaponBuildingAnimationDelay(art,tickHz);
}

int runtimeSuperWeaponBuildingFrame(const ArtType* art, int ageTicks, int tickHz) {
    if (!art) return 0;
    const int start = art->hasAnimationStart ? art->animationStart
        : art->hasAnimationLoopStart ? art->animationLoopStart : 0;
    const int step = std::max(0, ageTicks) / runtimeSuperWeaponBuildingFrameDelay(art, tickHz);
    if (art->hasAnimationEnd) {
        // NAMISL_F/H use 0..15, and the damaged halves use 16..31.
        // End is an absolute inclusive frame here, not a count to add to Start.
        return std::min(std::max(start, art->animationEnd), start + step);
    }
    const int loopStart = art->hasAnimationLoopStart ? art->animationLoopStart : start;
    const int span = art->hasAnimationLoopEnd ? std::max(1, art->animationLoopEnd - loopStart) : 1;
    return loopStart + step % span;
}

struct RuntimeSuperWeaponBuildingLayer {
    const ArtType::ActiveAnimation* layer = nullptr;
    int ageTicks = 0;
};

RuntimeSuperWeaponBuildingLayer runtimeSuperWeaponBuildingLayer(
    const WorldObjectInstance& object, const RuntimeMapRenderContext& context, const ArtType& art) {
    RuntimeSuperWeaponBuildingLayer result;
    if (std::none_of(art.auxiliaryAnimations.begin(), art.auxiliaryAnimations.end(),
            [](const auto& layer) { return runtimeSuperWeaponBuildingLayerKey(layer.key); })) return result;
    std::string key = "SuperAnim";
    if (context.specialMechanismSnapshots) {
        for (const auto& pose : *context.specialMechanismSnapshots) {
            if (pose.kind == "superWeaponBuilding" && pose.objectId == object.instanceId) {
                key = pose.resourceId;
                result.ageTicks = pose.progress;
                break;
            }
        }
    }
    const auto find = [&](const std::string& name) -> const ArtType::ActiveAnimation* {
        for (const auto& layer : art.auxiliaryAnimations) if (layer.key == name) return &layer;
        return nullptr;
    };
    result.layer = find(key);
    if (!result.layer) { result.layer = find("SuperAnim"); result.ageTicks = 0; }
    if (result.layer && context.art && (key == "SuperAnimTwo" || key == "SuperAnimFour")) {
        const auto& layer = *result.layer;
        const auto* animation = context.art->findArtType(runtimeObjectIsDamaged(object, context)
            && !layer.damagedImage.empty() ? layer.damagedImage : layer.image);
        if (animation && animation->hasAnimationEnd) {
            const int start = animation->hasAnimationStart ? animation->animationStart : 0;
            const int duration = std::max(1, animation->animationEnd - start + 1)
                * runtimeSuperWeaponBuildingFrameDelay(animation, context.fixedTickHz);
            if (result.ageTicks >= duration) {
                const auto* next = find(key == "SuperAnimTwo" ? "SuperAnimThree" : "SuperAnim");
                if (next) { result.layer = next; result.ageTicks -= duration; }
            }
        }
    }
    return result;
}
