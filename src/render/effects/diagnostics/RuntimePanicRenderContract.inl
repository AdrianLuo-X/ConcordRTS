bool runRuntimePanicRenderContractCheck(const RuntimeMapRenderContext& context, std::string& error) {
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    RuntimeMapRenderContext probe = context;
    probe.vision = nullptr; probe.specialMechanismSnapshots = nullptr;
    WorldObjectInstance object;
    object.kind = WorldObjectKind::Infantry; object.type = "CIV1";
    object.owner = "Americans"; object.alive = true; object.health = 100;
    object.hasRenderPosition = object.renderMoveActive = true; object.mission = "Panic";
    const auto* art = findArtTypeForObject(object, probe);
    const auto* panic = art ? findRuntimeSequenceFrame(*art, {"Panic"}) : nullptr;
    if (!panic || panic->startFrame != 134 || panic->frameCount != 6 || panic->facingMultiplier != 6) {
        error = "panic render: original civilian sequence missing"; return false;
    }
    for (int facing = 0; facing < 8; ++facing) for (int frame = 0; frame < 6; ++frame) {
        object.facing = facing * 32;
        object.movementAnimationDistance = (frame + 0.1) / 6.0;
        probe.animationTick = frame * 2;
        const auto* sprite = loadRuntimeObjectShpSpriteFromCache(object, probe, *cache);
        if (!sprite || !sprite->usedSequenceFrame || sprite->sequenceName != "Panic"
            || sprite->frameIndex != 134 + ((8 - facing) % 8) * 6 + frame
            || sprite->image.pixels.empty()) {
            error = "panic render: original facing/phase/pixels mismatch at "
                + std::to_string(facing) + "/" + std::to_string(frame)
                + " got " + (sprite ? sprite->sequenceName + "/" + std::to_string(sprite->frameIndex) : "missing");
            return false;
        }
    }
    object.renderMoveActive = false;
    const auto idle = runtimeSequenceCandidatesForObjectState(object, false);
    if (std::find(idle.begin(), idle.end(), "Panic") != idle.end()) {
        error = "panic render: blocked unit runs on the spot"; return false;
    }
    object.renderMoveActive = true;
    const auto wet = runtimeSequenceCandidatesForObjectState(object, true);
    if (wet.empty() || wet.front() != "Swim") {
        error = "panic render: panic overrides swimming"; return false;
    }
    return true;
}
