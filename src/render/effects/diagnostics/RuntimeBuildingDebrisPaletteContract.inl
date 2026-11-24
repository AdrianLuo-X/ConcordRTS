#include "render/effects/diagnostics/RuntimeBuildingDebrisPaletteContract.h"

bool runRuntimeBuildingDebrisPaletteContractCheck(
    const RuntimeMapRenderContext& context,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (context.art == nullptr) {
        errorMessage = "building debris art is missing";
        return false;
    }
    auto cache = std::make_unique<RuntimeMapRenderCache>();
    const auto* palette = runtimeAnimPaletteForRender(context, *cache);
    if (palette == nullptr) {
        errorMessage = "building debris animation palette is missing";
        return false;
    }

    constexpr int size = 192;
    constexpr std::uint32_t background = 0x00010203U;
    std::vector<std::uint32_t> pixels(size * size, background);
    std::vector<RuntimeProjectileRenderActor> actors(1);
    RuntimeMapRenderContext probe = context;
    probe.vision = nullptr;
    probe.surfacePixels = pixels.data();
    probe.surfaceWidth = size;
    probe.surfaceHeight = size;
    probe.projectileActors = &actors;
    RuntimeMapCamera camera;
    camera.worldOffsetX = -size / 2;
    camera.worldOffsetY = -size / 2;
    const auto viewport = runtimeRenderPlacementNativeRect(RuntimeInputPixelRect{0, 0, size, size});
    int typesChecked = 0;
    int framesChecked = 0;
    std::unordered_set<std::string> authoredDebris;
    if (context.rules != nullptr) {
        const auto collect = [&](const std::string& value) {
            std::istringstream stream(value); std::string id;
            while (std::getline(stream, id, ',')) authoredDebris.insert(toLowerAscii(trimAsciiForRender(id)));
        };
        collect(context.rules->getCombatDetailRules().metallicDebris);
        for (const auto& type : context.rules->getBuildingTypes()) collect(type.debrisAnims);
    }
    for (const ArtType& art : context.art->getArtTypes()) {
        if (!art.bouncer || art.voxel
            || (context.rules != nullptr ? authoredDebris.count(toLowerAscii(art.id)) == 0
                : toLowerAscii(art.id).rfind("dbris", 0) != 0)
            || !art.palette.empty() || !art.customPalette.empty()
            || !art.altPalette.empty() || art.translucent) {
            continue;
        }
        RuntimeProjectileRenderActor& actor = actors.front();
        actor.projectileId = art.id;
        actor.projectileImageId = art.image.empty() ? art.id : art.image;
        actor.renderIntent = "buildingDebris";
        actor.lifecycleState = "Travelling";
        actor.totalTicks = 60;
        actor.remainingTicks = 30;
        actor.hasCurrentPosition = true;
        auto* first = loadRuntimeAuxiliaryShpSpriteFromCache(
            actor.projectileImageId, 0, probe, *cache, false
        );
        if (first == nullptr) {
            errorMessage = "building debris sprite is missing: " + art.id;
            return false;
        }
        const int frameCount = first->frameCount;
        for (int frame = 0; frame < frameCount; ++frame) {
            actor.frameIndex = frame;
            auto* sprite = loadRuntimeAuxiliaryShpSpriteFromCache(
                actor.projectileImageId, frame, probe, *cache, false
            );
            if (sprite == nullptr) {
                errorMessage = art.id + " frame=" + std::to_string(frame);
                return false;
            }
            // Compare every opaque source pixel against the original animation
            // palette, including cached frames. A whole-scene yellow-pixel
            // count can pass while a different actor still draws purple debris.
            std::unordered_map<std::uint32_t, int> expectedColors;
            for (unsigned char index : sprite->image.pixels) {
                if (index != 0) {
                    ++expectedColors[packRuntimeDibColor((*palette)[index])];
                }
            }
            for (int repeat = 0; repeat < 2; ++repeat) {
                std::fill(pixels.begin(), pixels.end(), background);
                RuntimeMapRenderStats stats;
                drawRuntimeProjectileActors(
                    nullptr, viewport, probe, camera, *cache, stats
                );
                std::unordered_map<std::uint32_t, int> actualColors;
                for (std::uint32_t pixel : pixels) {
                    if (pixel != background) {
                        ++actualColors[pixel];
                    }
                }
                if (stats.projectileActorTravelDrawn != 1
                    || actualColors != expectedColors) {
                    errorMessage = art.id + " frame=" + std::to_string(frame)
                        + " cached=" + std::to_string(repeat)
                        + " did not render with the animation palette";
                    return false;
                }
            }
            ++framesChecked;
        }
        ++typesChecked;
    }
    if (typesChecked == 0 || framesChecked == 0) {
        errorMessage = "no building debris frames were checked";
        return false;
    }
    std::cout << "Building debris palette: types=" << typesChecked
        << " frames=" << framesChecked << " palette=anim.pal cached=yes\n";
    return true;
}
