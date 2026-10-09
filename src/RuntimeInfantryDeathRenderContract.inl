bool runRuntimeInfantryDeathRenderContractCheck(
    const RuntimeMapRenderContext& context, std::string& error
) {
    if (context.art == nullptr || context.rules == nullptr) {
        error = "infantry death render resources missing";
        return false;
    }
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    constexpr int size = 128;
    constexpr std::uint32_t background = 0x00242c30U;
    std::vector<std::uint32_t> pixels(size * size, background);
    std::vector<std::uint32_t> sheet(size * 5 * size * 4, background);
    RuntimeMapRenderContext probe = context;
    probe.vision = nullptr;
    probe.surfacePixels = pixels.data();
    probe.surfaceWidth = size;
    probe.surfaceHeight = size;
    RuntimeMapCamera camera;
    camera.worldOffsetX = -size / 2;
    camera.worldOffsetY = -size * 3 / 4;
    camera.initialized = true;
    const RECT viewport{0, 0, size, size};
    GameWorld world;
    int rows = 0;
    int checkedFrames = 0;
    for (const char* type : {"E1", "E2", "ENGINEER", "DOG"}) {
        const UnitType* unit = context.rules->findUnitType("InfantryType", type);
        const auto sequence = unit == nullptr ? std::nullopt
            : runtimeInfantryDeathSequence(*context.art, *unit, 1, 96, false, runtimeRenderUsesArcticArt(context));
        if (!sequence) { error = std::string(type) + " death sequence missing"; return false; }
        const ArtType* art = context.art->findArtType(sequence->artId);
        const bool splitShadow = runtimeArtShadowEnabled(art, true);
        for (int offset = 0; offset < sequence->frameCount; ++offset) {
            const int index = sequence->startFrame + offset;
            const auto* frame = loadRuntimeAuxiliaryShpSpriteFromCache(
                sequence->artId, index, probe, *cache, splitShadow);
            if (frame == nullptr || frame->frameIndex != index) {
                error = std::string(type) + " death frame missing or wrapped";
                return false;
            }
            ++checkedFrames;
        }
        RuntimeImpactRenderActor actor;
        actor.animId = sequence->artId;
        actor.renderIntent = "infantryDeath";
        actor.owner = "Americans";
        actor.totalTicks = 60;
        actor.remainingTicks = 60;
        actor.fadeTicks = 15;
        actor.frameIndex = sequence->startFrame;
        const auto draw = [&]() {
            std::fill(pixels.begin(), pixels.end(), background);
            RuntimeMapRenderStats stats;
            drawRuntimeImpactActor(nullptr, viewport, world, probe, camera, *cache, actor, stats);
            return stats.impactActorDrawn == 1 && std::any_of(pixels.begin(), pixels.end(),
                [](std::uint32_t pixel) { return pixel != background; });
        };
        if (!draw()) { error = std::string(type) + " corpse not drawn"; return false; }
        const auto bluePixels = pixels;
        actor.owner = "Russians";
        if (!draw() || pixels == bluePixels) {
            error = std::string(type) + " corpse lost its owner color"; return false;
        }
        const std::vector<RuntimeImpactRenderActor> actors{actor};
        const auto records = buildRuntimeGroundBodyDrawRecords({}, {}, 0, camera, &actors, &probe);
        if (records.size() != 1 || records.front().key.kind != RuntimeGroundBodyKind::GroundImpact
            || runtimeImpactActorSpriteOffsetY(actor, camera) != 0) {
            error = "corpse ground sorting/foot anchor mismatch"; return false;
        }
        for (int column = 0; column < 5; ++column) {
            actor.frameIndex = sequence->startFrame
                + std::min(sequence->frameCount - 1, column * (sequence->frameCount - 1) / 3);
            actor.remainingTicks = column == 4 ? 5 : 60;
            if (!draw()) { error = "death animation phase not drawn"; return false; }
            for (int y = 0; y < size; ++y) {
                std::copy_n(pixels.begin() + y * size, size,
                    sheet.begin() + (rows * size + y) * size * 5 + column * size);
            }
        }
        ++rows;
    }
    if (const char* path = std::getenv("CONCORD_DEATH_ANIMATION_BMP")) {
        if (*path != '\0' && !writeRuntimeBgraBmp(path, size * 5, size * 4, sheet)) {
            error = "could not write infantry death preview"; return false;
        }
    }
    std::cout << "Infantry death render: types=" << rows << " frames=" << checkedFrames
        << " ownerRemap=ok ground=ok fade=ok\n";
    return true;
}
