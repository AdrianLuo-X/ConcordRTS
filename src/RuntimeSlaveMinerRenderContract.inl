bool runRuntimeSlaveMinerRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& error
) {
    const auto fail = [&](const std::string& message) {
        error = "slave miner animation: " + message;
        return false;
    };
    RuntimeMapRenderContext probe = context;
    probe.vision = nullptr;
    probe.mapFacts = nullptr;
    probe.specialMechanismSnapshots = nullptr;
    probe.fixedTickHz = 30;
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    WorldObjectInstance worker;
    worker.instanceId = 7;
    worker.kind = WorldObjectKind::Infantry;
    worker.type = "SLAV";
    worker.owner = "YuriCountry";
    worker.alive = true;
    worker.health = 125;
    worker.hasRenderPosition = true;
    const auto* art = findArtTypeForObject(worker, probe);
    if (art == nullptr) return fail("SLAV art missing");
    const auto* shovel = findRuntimeSequenceFrame(*art, {"Shovel"});
    const auto* carry = findRuntimeSequenceFrame(*art, {"Carry"});
    if (shovel == nullptr || shovel->startFrame != 116 || shovel->frameCount != 6
        || shovel->facingMultiplier != 6 || carry == nullptr || carry->startFrame != 228
        || carry->frameCount != 6 || carry->facingMultiplier != 6)
        return fail("authored Shovel/Carry strips were not parsed");
    constexpr int panelWidth = 100, panelHeight = 96;
    constexpr int width = panelWidth * 6, height = panelHeight * 3;
    constexpr std::uint32_t background = 0x003b463bU;
    std::vector<std::uint32_t> pixels(width * height, background);
    probe.surfacePixels = pixels.data();
    probe.surfaceWidth = width;
    probe.surfaceHeight = height;
    RuntimeMapCamera camera;
    camera.initialized = true;
    for (int row = 0; row < 3; ++row) {
        worker.mission = row == 0 ? "HarvestLoad" : row == 1 ? "Return" : "Harvest";
        worker.renderMoveActive = row > 0;
        const std::string sequence = row == 0 ? "Shovel" : row == 1 ? "Carry" : "Walk";
        const int start = row == 0 ? 116 : row == 1 ? 228 : 8;
        for (int facing = 0; facing < 8; ++facing) {
            worker.facing = facing * 32;
            std::unordered_set<int> frames;
            for (int phase = 0; phase < 6; ++phase) {
                probe.animationTick = phase * runtimeInfantrySequenceDelayTicks(art, probe.fixedTickHz);
                worker.movementAnimationDistance = (phase + 0.1) / 6.0;
                const auto* sprite = loadRuntimeObjectShpSpriteFromCache(worker, probe, *cache);
                const int directionStart = start + ((8 - facing) % 8) * 6;
                if (sprite == nullptr || !sprite->usedSequenceFrame || sprite->sequenceName != sequence
                    || sprite->frameIndex < directionStart || sprite->frameIndex >= directionStart + 6
                    || sprite->image.pixels.empty()) return fail(sequence + " direction/phase missing");
                frames.insert(sprite->frameIndex);
                const int first = sprite->frameIndex;
                if (loadRuntimeObjectShpSpriteFromCache(worker, probe, *cache)->frameIndex != first)
                    return fail("paused frame changed on repaint");
                probe.fixedTickHz = 60;
                probe.animationTick *= 2;
                if (loadRuntimeObjectShpSpriteFromCache(worker, probe, *cache)->frameIndex != first)
                    return fail("tick frequency changed animation speed");
                probe.fixedTickHz = 30;
                probe.animationTick /= 2;
                if (facing == 2) {
                    const RECT viewport{phase * panelWidth, row * panelHeight,
                        (phase + 1) * panelWidth, (row + 1) * panelHeight};
                    const POINT center{viewport.left + panelWidth / 2, viewport.top + 72};
                    if (!tryDrawObjectShpSprite(nullptr, viewport, worker, center, probe, camera, *cache))
                        return fail("original worker sprite could not be drawn");
                }
            }
            if (frames.size() != 6) return fail(sequence + " did not play a complete loop");
        }
    }
    worker.renderMoveActive = false;
    for (const char* mission : {"Guard", "Harvest", "HarvestAlign", "Return"}) {
        worker.mission = mission;
        const auto* sprite = loadRuntimeObjectShpSpriteFromCache(worker, probe, *cache);
        if (sprite == nullptr || (sprite->sequenceName != "Guard" && sprite->sequenceName != "Ready"))
            return fail("worker dug while idle, aligning or waiting to return");
    }
    worker.mission = "Attack";
    if (loadRuntimeObjectShpSpriteFromCache(worker, probe, *cache)->sequenceName != "FireUp")
        return fail("freed worker lost its attack animation");
    ArtType fallback = *art;
    fallback.sequenceFrames.erase(std::remove_if(fallback.sequenceFrames.begin(), fallback.sequenceFrames.end(),
        [](const auto& frame) { return frame.name == "Shovel" || frame.name == "Carry"; }),
        fallback.sequenceFrames.end());
    worker.mission = "HarvestLoad";
    const auto fallbackFrame = selectRuntimeObjectSequenceFrame(worker, probe, &fallback, 300);
    if (!fallbackFrame.matched || fallbackFrame.sequenceName != "Ready")
        return fail("missing mod mining strip did not fall back to Ready");
    if (const char* path = std::getenv("CONCORD_SLAVE_MINER_BMP")) {
        if (*path != '\0' && !writeRuntimeBgraBmp(path, width, height, pixels))
            return fail("preview write failed");
    }
    std::cout << "Slave miner animation: Shovel/Carry/Walk=144 directional frames "
        "pause=ok tickRate=ok fallback=ok attack=ok\n";
    return true;
}
