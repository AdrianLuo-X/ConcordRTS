#include "render/core/RuntimeMapTerrainFrame.h"

#include <bit>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <type_traits>

int main() {
    MapFacts facts;
    facts.lighting.base = {0.7, 0.8, 0.9, 1.1, 0.0, 0.032};
    facts.lighting.ion = {0.45, 0.5, 0.6, 1.0, 0.0, 0.032};
    RuntimeLightingSystemState lighting(facts.lighting);
    const auto* theater = reinterpret_cast<const TheaterDatabase*>(std::uintptr_t{16});
    const auto* art = reinterpret_cast<const ArtDatabase*>(std::uintptr_t{32});
    const auto* rules = reinterpret_cast<const RulesDatabase*>(std::uintptr_t{48});
    const auto* vision = reinterpret_cast<const RuntimeVisionSystem*>(std::uintptr_t{64});
    const auto sameTint = [](const RuntimeLightingTint& a, const RuntimeLightingTint& b) {
        return std::bit_cast<std::uint64_t>(a.red) == std::bit_cast<std::uint64_t>(b.red)
            && std::bit_cast<std::uint64_t>(a.green) == std::bit_cast<std::uint64_t>(b.green)
            && std::bit_cast<std::uint64_t>(a.blue) == std::bit_cast<std::uint64_t>(b.blue);
    };
    std::ostringstream original;
    lighting.save(original);
    const auto originalHash = lighting.stateHash();
    int cases = 0;
    for (int mask = 0; mask < 256; ++mask) {
        for (int delta = -1; delta <= 1; ++delta) {
            RuntimeMapTerrainFrameSources input;
            input.theater = theater; input.theaterLoaded = (mask & 1) != 0;
            input.art = art; input.artLoaded = (mask & 2) != 0;
            input.rules = rules; input.rulesLoaded = (mask & 4) != 0;
            input.mapFacts = &facts; input.mapFactsLoaded = (mask & 8) != 0;
            input.vision = vision; input.visionRestricted = (mask & 16) != 0;
            input.visionObserverOwner = (mask & 32) ? "Americans" : "";
            input.campaignMode = (mask & 64) != 0;
            input.tick = (mask & 128) ? -1 : 42;
            input.campaignIonStormUntilTick = input.tick + delta;
            const auto frame = prepareRuntimeMapTerrainFrame(input, lighting);
            const bool ion = input.campaignMode && delta > 0 && input.mapFactsLoaded;
            const auto tint = ion ? runtimeLightingTintForProfile(facts.lighting.ion)
                : lighting.renderTint();
            if (frame.theater != (input.theaterLoaded ? theater : nullptr)
                || frame.art != (input.artLoaded ? art : nullptr)
                || frame.rules != (input.rulesLoaded ? rules : nullptr)
                || frame.mapFacts != (input.mapFactsLoaded ? &facts : nullptr)
                || frame.vision != (input.visionRestricted ? vision : nullptr)
                || frame.visionObserverOwner != (input.visionRestricted ? input.visionObserverOwner : "")
                || frame.lightingProfile != (ion ? &facts.lighting.ion : &lighting.currentProfile())
                || !sameTint(frame.lightingTint, tint)) {
                std::cerr << "frame input mismatch mask=" << mask << " delta=" << delta << '\n';
                return 1;
            }
            const auto owner = frame.visionObserverOwner;
            input.visionObserverOwner = "source changed after preparation";
            if (frame.visionObserverOwner != owner) return 2;
            ++cases;
        }
    }
    std::ostringstream after;
    lighting.save(after);
    if (after.str() != original.str() || lighting.stateHash() != originalHash) return 3;
    static_assert(std::is_copy_constructible_v<RuntimeMapTerrainFrame>);
    std::cout << "Map terrain frame: " << cases
        << " readiness/observer/campaign/tick cases, exact tint bits, owned observer text; full lighting state unchanged.\n";
    return 0;
}
