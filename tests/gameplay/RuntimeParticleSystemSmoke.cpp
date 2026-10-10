#include "data/art/ArtDatabase.h"
#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/effects/RuntimeParticleSystem.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <iostream>
#include <sstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "particle smoke: " << message << "\n";
        return false;
    }
    return true;
}

bool samePoint(
    const RuntimeParticlePoint& left,
    const RuntimeParticlePoint& right
) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool near(double left, double right, double epsilon = 1.0e-9) {
    return std::abs(left - right) <= epsilon;
}

bool sameEvents(
    const std::vector<RuntimeParticleDamageEvent>& left,
    const std::vector<RuntimeParticleDamageEvent>& right
) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const RuntimeParticleDamageEvent& a = left[index];
        const RuntimeParticleDamageEvent& b = right[index];
        if (a.tick != b.tick
            || a.systemInstanceId != b.systemInstanceId
            || a.particleInstanceId != b.particleInstanceId
            || a.sourceObjectId != b.sourceObjectId
            || a.owner != b.owner
            || a.sourceTag != b.sourceTag
            || a.particleTypeId != b.particleTypeId
            || a.warheadId != b.warheadId
            || !samePoint(a.position, b.position)
            || a.damage != b.damage
            || a.radius != b.radius) {
            return false;
        }
    }
    return true;
}

bool replaceOnce(
    std::string& text,
    const std::string& from,
    const std::string& to
) {
    const std::size_t offset = text.find(from);
    if (offset == std::string::npos) {
        return false;
    }
    text.replace(offset, from.size(), to);
    return true;
}

RuntimeParticleSpawnContext virusContext() {
    RuntimeParticleSpawnContext context;
    context.sourceObjectId = 77;
    context.owner = "Yuri;Prime";
    context.sourceTag = "VIRUSD/InfDeath=8";
    context.position = {12.0, 24.0, 3.0};
    context.seed = 0x123456789abcdef0ULL;
    return context;
}

int runPerformanceBenchmark() {
    IniFile ini;
    RulesDatabase rules;
    if (!ini.loadFromString(
            "[Particles]\n0=StressParticle\n"
            "[ParticleSystems]\n0=StressSystem\n"
            "[StressParticle]\nBehavesLike=Smoke\nMaxEC=2000\n"
            "ColorList=(255,120,30),(60,60,60)\nColorSpeed=.01\n"
            "[StressSystem]\nHoldsWhat=StressParticle\nBehavesLike=Smoke\n"
            "Spawns=yes\nSpawnFrames=1\nLightSize=24\nLifetime=1000\n"
        ) || !rules.loadFromIni(ini)) {
        return 2;
    }
    RuntimeParticleSystemState state;
    RuntimeParticleSpawnContext context;
    for (int index = 0; index < 2048; ++index) {
        context.sourceObjectId = index;
        context.position = {static_cast<double>(index * 16), 32.0, 64.0};
        if (state.spawnSystem(rules, "StressSystem", context) == 0) {
            return 3;
        }
    }
    for (int tick = 0; tick < 32; ++tick) {
        (void)state.tick(rules);
    }
    std::uint64_t lightChecksum = 0;
    std::uint64_t renderChecksum = 0;
    constexpr int repetitions = 30;
    const auto lightsStart = std::chrono::steady_clock::now();
    for (int repetition = 0; repetition < repetitions; ++repetition) {
        for (const auto& snapshot : state.localLightSnapshots(rules)) {
            lightChecksum = lightChecksum * 1099511628211ULL
                + snapshot.revision;
        }
    }
    const auto renderStart = std::chrono::steady_clock::now();
    for (int repetition = 0; repetition < repetitions; ++repetition) {
        for (const auto& snapshot : state.renderSnapshots(rules)) {
            renderChecksum = renderChecksum * 1099511628211ULL
                + snapshot.revision;
        }
    }
    const auto finished = std::chrono::steady_clock::now();
    RuntimeParticleSystemState tickState = state;
    for (int tick = 0; tick < 40; ++tick) {
        (void)tickState.tick(rules);
    }
    constexpr int tickRepetitions = 180;
    const auto ticksStart = std::chrono::steady_clock::now();
    for (int tick = 0; tick < tickRepetitions; ++tick) {
        (void)tickState.tick(rules);
    }
    const auto ticksFinished = std::chrono::steady_clock::now();
    std::cout << "particle benchmark systems=" << state.systems().size()
        << " particles=" << state.particles().size()
        << " light_ms=" << std::chrono::duration<double, std::milli>(
            renderStart - lightsStart).count() / repetitions
        << " render_ms=" << std::chrono::duration<double, std::milli>(
            finished - renderStart).count() / repetitions
        << " light_checksum=" << lightChecksum
        << " render_checksum=" << renderChecksum
        << " tick_ms=" << std::chrono::duration<double, std::milli>(
            ticksFinished - ticksStart).count() / tickRepetitions
        << " tick_state_hash=" << tickState.stateHash()
        << " state_hash=" << state.stateHash() << '\n';
    return 0;
}

bool verifySnapshotOrderAfterLoad(const RulesDatabase& rules) {
    RuntimeParticleSystemState original;
    auto context = virusContext();
    context.direction = {32.0, 0.0, 0.0};
    for (int index = 0; index < 12; ++index) {
        context.position.x = static_cast<double>(index * 256);
        original.spawnSystem(rules, "RailgunSys", context);
        original.spawnSystem(rules, "SparkSys", context);
        original.spawnSystem(rules, "GasCloudSys", context);
    }
    (void)original.tick(rules);
    std::ostringstream saved;
    original.save(saved);
    std::string shuffled = saved.str();
    const auto reverseSections = [&shuffled](const std::string& kind,
        std::size_t count) {
        for (std::size_t index = 0; index < count / 2U; ++index) {
            const std::string first = "[ParticleRuntime." + kind + "."
                + std::to_string(index) + "]";
            const std::string last = "[ParticleRuntime." + kind + "."
                + std::to_string(count - index - 1U) + "]";
            if (!replaceOnce(shuffled, first, "[ParticleRuntime.Swap]")
                || !replaceOnce(shuffled, last, first)
                || !replaceOnce(shuffled, "[ParticleRuntime.Swap]", last)) {
                return false;
            }
        }
        return true;
    };
    IniFile shuffledIni;
    RuntimeParticleSystemState restored;
    if (!reverseSections("System", original.systems().size())
        || !reverseSections("Particle", original.particles().size())
        || !shuffledIni.loadFromString(shuffled)
        || !restored.load(shuffledIni, rules)) {
        return expect(false, "shuffled particle save failed to load");
    }
    const auto sameRevisions = [](const auto& left, const auto& right) {
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
            if (left[index].revision != right[index].revision) {
                return false;
            }
        }
        return true;
    };
    for (int tick = 0; tick < 50; ++tick) {
        if (original.stateHash() != restored.stateHash()
            || !sameRevisions(original.renderSnapshots(rules),
                restored.renderSnapshots(rules))
            || !sameRevisions(original.beamSnapshots(rules),
                restored.beamSnapshots(rules))
            || !sameRevisions(original.localLightSnapshots(rules),
                restored.localLightSnapshots(rules))
            || !sameEvents(original.tick(rules), restored.tick(rules))) {
            return expect(false,
                "shuffled load changed snapshot order, expiry or damage events");
        }
    }
    return true;
}

bool verifyLongRunningParticleCleanup() {
    IniFile ini;
    RulesDatabase rules;
    if (!ini.loadFromString(
            "[Particles]\n0=SoakPuff\n1=SoakFade\n"
            "[ParticleSystems]\n0=SoakEmitter\n"
            "[SoakPuff]\nBehavesLike=Smoke\nMaxEC=4\nNextParticle=SoakFade\n"
            "[SoakFade]\nBehavesLike=Smoke\nMaxEC=3\n"
            "[SoakEmitter]\nHoldsWhat=SoakPuff\nBehavesLike=Smoke\n"
            "Spawns=yes\nSpawnFrames=1\nLifetime=2\nParticleCap=2\n"
        ) || !rules.loadFromIni(ini)) {
        return expect(false, "particle soak fixture failed to load");
    }
    RuntimeParticleSystemState state;
    auto context = virusContext();
    std::size_t maximumSystems = 0;
    std::size_t maximumParticles = 0;
    // Repeated expiry leaves live IDs increasingly far from zero. A lookup
    // must track the live population, including NextParticle descendants.
    for (int tick = 0; tick < 12000; ++tick) {
        for (int emitter = 0; emitter < 4; ++emitter) {
            context.sourceObjectId = emitter;
            if (state.spawnSystem(rules, "SoakEmitter", context) == 0) {
                return expect(false, "particle soak exhausted system capacity");
            }
        }
        if (state.spawnParticle(rules, "SoakPuff", context) == 0) {
            return expect(false, "particle soak exhausted particle capacity");
        }
        (void)state.tick(rules);
        maximumSystems = std::max(maximumSystems, state.systems().size());
        maximumParticles = std::max(maximumParticles, state.particles().size());
        if (maximumSystems > 96U || maximumParticles > 192U) {
            return expect(false, "expired particle population grew during soak");
        }
        if ((tick + 1) % 3000 == 0) {
            std::ostringstream saved;
            state.save(saved);
            IniFile savedIni;
            RuntimeParticleSystemState restored;
            if (!savedIni.loadFromString(saved.str())
                || !restored.load(savedIni, rules)
                || restored.stateHash() != state.stateHash()) {
                return expect(false, "particle soak save/load changed state");
            }
            state = std::move(restored);
        }
    }
    for (int tick = 0; tick < 32; ++tick) {
        (void)state.tick(rules);
    }
    return expect(state.systems().empty() && state.particles().empty()
            && state.renderSnapshots(rules).empty()
            && state.localLightSnapshots(rules).empty(),
        "particle soak left expired systems, particles or render snapshots");
}

bool verifyEmissionScreenOffset() {
    IniFile ini;
    RulesDatabase rules;
    if (!ini.loadFromString(
            "[Particles]\n0=Plume\n1=After\n[ParticleSystems]\n0=Outlet\n"
            "[Plume]\nBehavesLike=Smoke\nImage=SGRYSMK1\nMaxEC=8\nVelocity=0\n"
            "NextParticle=After\nNextParticleOffset=0,0,10\n"
            "[After]\nBehavesLike=Smoke\nImage=SGRYSMK1\nMaxEC=8\nVelocity=0\n"
            "[Outlet]\nBehavesLike=Smoke\nHoldsWhat=Plume\nSpawns=yes\n"
            "SpawnFrames=1\nParticleCap=32\nLifetime=30\n")
        || !rules.loadFromIni(ini)) return expect(false, "screen offset fixture");

    RuntimeParticleSystemState state(30), unlifted(30), ordinary(30);
    RuntimeParticleSpawnContext context;
    context.position = {1024.0, 2048.0, 750.0};
    context.direction = {0.0, 0.0, 1.0};
    context.screenOffsetY = -34;
    context.sourceTag = "vehicle-damage-smoke";
    const auto id = state.spawnSystem(rules, "Outlet", context);
    auto ordinaryContext = context;
    ordinaryContext.sourceTag = "ordinary-smoke";
    const auto ordinaryId = ordinary.spawnSystem(rules, "Outlet", ordinaryContext);
    context.screenOffsetY = 0;
    const auto plainId = unlifted.spawnSystem(rules, "Outlet", context);
    (void)state.tick(rules); (void)unlifted.tick(rules); (void)ordinary.tick(rules);
    if (!expect(id != 0 && plainId != 0 && ordinaryId != 0
            && state.particles().size() == 1 && ordinary.particles().size() == 1,
            "screen offset producer emits a particle")) return false;
    const auto initial = state.particles().front();
    const auto liftedSnapshot = state.renderSnapshots(rules).front();
    const auto plainSnapshot = unlifted.renderSnapshots(rules).front();
    const auto ordinarySnapshot = ordinary.renderSnapshots(rules).front();
    bool ok = expect(initial.screenOffsetY == -34 && liftedSnapshot.screenOffsetY == -34
            && samePoint(initial.position, unlifted.particles().front().position)
            && liftedSnapshot.rawZ == plainSnapshot.rawZ
            && liftedSnapshot.revision != plainSnapshot.revision
            && state.stateHash() != unlifted.stateHash(),
        "emission screen offset changes presentation and hash without changing world position");
    ok &= expect(liftedSnapshot.airborneDamageSmoke
            && !plainSnapshot.airborneDamageSmoke && !ordinarySnapshot.airborneDamageSmoke
            && ordinarySnapshot.screenOffsetY == liftedSnapshot.screenOffsetY
            && ordinarySnapshot.revision != liftedSnapshot.revision,
        "only airborne damage smoke enters the air layer and its layer changes the render revision");
    ok &= expect(state.moveSystem(id, {2048.0, 3072.0, 0.0}, "Americans", 0)
            && state.systems().front().screenOffsetY == 0
            && state.particles().front().screenOffsetY == -34
            && samePoint(state.particles().front().position, initial.position),
        "landed producer updates without pulling an existing airborne puff");
    for (int tick = 0; tick < 3; ++tick) (void)state.tick(rules);
    ok &= expect(state.particles().size() > 1
            && state.particles().back().screenOffsetY == 0
            && state.particles().front().screenOffsetY == -34,
        "later emissions inherit the producer's new screen offset");
    const auto mixedSnapshots = state.renderSnapshots(rules);
    ok &= expect(mixedSnapshots.front().airborneDamageSmoke
            && !mixedSnapshots.back().airborneDamageSmoke,
        "a producer landing does not move an earlier puff out of the air layer");

    std::ostringstream saved;
    state.save(saved);
    IniFile savedIni;
    RuntimeParticleSystemState restored;
    ok &= expect(savedIni.loadFromString(saved.str()) && restored.load(savedIni, rules)
            && restored.stateHash() == state.stateHash(),
        "mixed airborne and landed particle offsets survive save/load");
    const auto restoredSnapshots = restored.renderSnapshots(rules);
    ok &= expect(restoredSnapshots.size() == mixedSnapshots.size()
            && std::equal(restoredSnapshots.begin(), restoredSnapshots.end(), mixedSnapshots.begin(),
                [](const auto& left, const auto& right) {
                    return left.airborneDamageSmoke == right.airborneDamageSmoke
                        && left.revision == right.revision;
                }),
        "save/load derives the same particle layer and render revision");
    for (int tick = 0; tick < 14; ++tick) {
        (void)state.tick(rules); (void)restored.tick(rules);
        ok &= expect(state.stateHash() == restored.stateHash(), "screen offset replay diverged");
    }
    ok &= expect(std::any_of(state.particles().begin(), state.particles().end(),
            [](const auto& particle) { return particle.typeId == "After" && particle.screenOffsetY == -34; }),
        "NextParticle inherits the emitted puff offset, not the current producer offset");
    const auto chainedSnapshots = state.renderSnapshots(rules);
    ok &= expect(std::any_of(chainedSnapshots.begin(), chainedSnapshots.end(),
            [](const auto& snapshot) { return snapshot.particleTypeId == "After" && snapshot.airborneDamageSmoke; }),
        "a chained puff retains airborne damage smoke provenance after its source landed");

    std::string legacy = saved.str();
    for (std::size_t at = legacy.find("ScreenOffsetY="); at != std::string::npos;
            at = legacy.find("ScreenOffsetY=")) {
        const auto end = legacy.find('\n', at);
        legacy.erase(at, end == std::string::npos ? std::string::npos : end - at + 1);
    }
    IniFile legacyIni;
    RuntimeParticleSystemState legacyState;
    ok &= expect(legacyIni.loadFromString(legacy) && legacyState.load(legacyIni, rules)
            && std::all_of(legacyState.systems().begin(), legacyState.systems().end(),
                [](const auto& system) { return system.screenOffsetY == 0; })
            && std::all_of(legacyState.particles().begin(), legacyState.particles().end(),
                [](const auto& particle) { return particle.screenOffsetY == 0; }),
        "legacy Version 1 saves default absent screen offsets to zero");
    const auto legacySnapshots = legacyState.renderSnapshots(rules);
    ok &= expect(std::none_of(legacySnapshots.begin(), legacySnapshots.end(),
            [](const auto& snapshot) { return snapshot.airborneDamageSmoke; }),
        "legacy puffs without an airborne emission offset keep their original layer");
    for (const bool producer : {true, false}) for (const auto* invalid : {"4097", "-4097", "bad"}) {
        std::string corrupted = saved.str();
        const std::string field = producer ? "ScreenOffsetY=0\n" : "ScreenOffsetY=-34\n";
        ok &= expect(replaceOnce(corrupted, field, std::string("ScreenOffsetY=") + invalid + "\n"),
            "invalid offset fixture field");
        IniFile corruptedIni;
        std::string error;
        const auto before = restored.stateHash();
        ok &= expect(corruptedIni.loadFromString(corrupted) && !restored.load(corruptedIni, rules, &error)
                && !error.empty() && restored.stateHash() == before,
            "invalid producer/particle screen offset rejects atomically");
    }
    const auto beforeRejected = state.stateHash();
    context.screenOffsetY = 4097;
    std::string error;
    ok &= expect(!state.moveSystem(id, context.position, "Americans", -4097)
            && state.spawnSystem(rules, "Outlet", context, &error) == 0
            && state.spawnParticle(rules, "Plume", context, &error) == 0
            && state.stateHash() == beforeRejected,
        "out-of-range offsets cannot enter live state through spawn or move");
    return ok;
}

bool verifyEmissionPresentation() {
    IniFile fixture;
    RulesDatabase rules;
    if (!fixture.loadFromString(
            "[Particles]\n0=Plume\n1=After\n[ParticleSystems]\n0=Outlet\n"
            "[Plume]\nBehavesLike=Smoke\nImage=SGRYSMK1\nMaxEC=8\nVelocity=0\n"
            "NextParticle=After\nNextParticleOffset=0,0,10\n"
            "[After]\nBehavesLike=Smoke\nImage=SGRYSMK1\nMaxEC=8\nVelocity=0\n"
            "[Outlet]\nBehavesLike=Smoke\nHoldsWhat=Plume\nSpawns=yes\n"
            "SpawnFrames=1\nParticleCap=32\nLifetime=30\n")
        || !rules.loadFromIni(fixture)) return expect(false, "presentation fixture");

    RuntimeParticleSpawnContext birth;
    birth.sourceObjectId = 77;
    birth.sourceTag = "vehicle-damage-smoke";
    birth.owner = "Americans";
    birth.position = {1024.0, 2048.0, 750.0};
    birth.screenOffsetY = -34;
    birth.presentation.enabled = true;
    birth.presentation.sourceCellSum = 12.375;
    birth.presentation.sourceLevel = 2.9296875;
    birth.presentation.roofs = {{12.0, 0.0, 4, 0, false},
        {11.25, 1.5, 0, 97, true}};
    birth.spriteAttachment = {12, -4, 3, -8, true, true};
    const auto savedText = [](const RuntimeParticleSystemState& state) {
        std::ostringstream text;
        state.save(text);
        return text.str();
    };
    const auto sameSnapshots = [](const auto& left, const auto& right) {
        return left.size() == right.size()
            && std::equal(left.begin(), left.end(), right.begin(), [](const auto& a, const auto& b) {
                return a.instanceId == b.instanceId && a.revision == b.revision
                    && a.presentation == b.presentation && a.spriteAttachment == b.spriteAttachment
                    && a.airborneDamageSmoke == b.airborneDamageSmoke;
            });
    };

    RuntimeParticleSystemState state(15);
    const auto producerId = state.spawnSystem(rules, "Outlet", birth);
    // Spawning a Smoke system creates its producer. Its first puff is emitted
    // by the next simulation tick, unlike a one-shot Gas system.
    (void)state.tick(rules);
    if (!expect(producerId != 0 && state.particles().size() == 1U,
            "presentation emitter must emit its first puff on the first tick")) return false;
    const auto originalParticle = state.particles().front();
    const auto originalSnapshots = state.renderSnapshots(rules);
    bool ok = expect(originalParticle.presentation == birth.presentation
            && originalParticle.spriteAttachment == birth.spriteAttachment
            && originalSnapshots.front().presentation == birth.presentation
            && originalSnapshots.front().spriteAttachment == birth.spriteAttachment,
        "emission copies camera-independent roof geometry and all sprite attachment components");

    auto egress = birth;
    egress.position = {2048.0, 4096.0, 800.0};
    egress.screenOffsetY = 0;
    egress.presentation.sourceCellSum = 24.0;
    egress.presentation.sourceLevel = 3.125;
    egress.presentation.factoryLiftCancellation = 0;
    egress.presentation.roofs.clear();
    egress.spriteAttachment = {21, -10, -3, 5, true, false};
    ok &= expect(state.moveSystem(producerId, egress.position, egress.owner, egress.screenOffsetY,
            egress.presentation, egress.spriteAttachment)
            && state.systems().front().presentation == egress.presentation
            && state.systems().front().spriteAttachment == egress.spriteAttachment
            && samePoint(state.particles().front().position, originalParticle.position)
            && sameSnapshots(state.renderSnapshots(rules), originalSnapshots),
        "moving producer to a factory changes only future emissions, including the zero cancellation sentinel");
    (void)state.tick(rules);
    ok &= expect(state.particles().size() == 2U
            && state.particles().front().presentation == birth.presentation
            && state.particles().back().presentation == egress.presentation
            && state.particles().back().spriteAttachment == egress.spriteAttachment,
        "later emission receives the updated factory profile without rewriting the old roof profile");
    const auto beforeLanding = state.renderSnapshots(rules);
    ok &= expect(state.moveSystem(producerId, {3072.0, 4096.0, 0.0}, birth.owner)
            && state.systems().front().presentation == RuntimeParticleAirbornePresentation{}
            && state.systems().front().spriteAttachment == RuntimeParticleSpriteAttachment{}
            && sameSnapshots(state.renderSnapshots(rules), beforeLanding),
        "landing clears the producer attachment but preserves every existing puff revision");
    (void)state.tick(rules);
    ok &= expect(state.particles().size() == 3U
            && state.particles().back().presentation == RuntimeParticleAirbornePresentation{}
            && state.particles().back().spriteAttachment == RuntimeParticleSpriteAttachment{},
        "landed producer emits a plain puff");
    const auto beforeSourceRemoval = state.renderSnapshots(rules);
    ok &= expect(state.stopSystemsForSource(birth.sourceObjectId) == 1U
            && sameSnapshots(state.renderSnapshots(rules), beforeSourceRemoval),
        "source removal stops production without losing emitted geometry or attachment");

    RuntimeParticleSystemState restored;
    IniFile archive;
    if (!expect(archive.loadFromString(savedText(state)) && restored.load(archive, rules)
            && restored.stateHash() == state.stateHash()
            && sameSnapshots(restored.renderSnapshots(rules), state.renderSnapshots(rules)),
            "mixed roof, factory and plain smoke survives full save/load")) return false;
    bool inheritedRoof = false, inheritedFactory = false, inheritedPlain = false;
    for (int tick = 0; tick < 20; ++tick) {
        const auto expectedEvents = state.tick(rules);
        const auto actualEvents = restored.tick(rules);
        ok &= expect(sameEvents(expectedEvents, actualEvents)
                && state.stateHash() == restored.stateHash()
                && sameSnapshots(state.renderSnapshots(rules), restored.renderSnapshots(rules)),
            "saved presentation replay diverged at tick " + std::to_string(tick));
        for (const auto& particle : state.particles()) {
            if (particle.typeId != "After") continue;
            inheritedRoof |= particle.presentation == birth.presentation
                && particle.spriteAttachment == birth.spriteAttachment;
            inheritedFactory |= particle.presentation == egress.presentation
                && particle.spriteAttachment == egress.spriteAttachment;
            inheritedPlain |= particle.presentation == RuntimeParticleAirbornePresentation{}
                && particle.spriteAttachment == RuntimeParticleSpriteAttachment{};
        }
    }
    ok &= expect(inheritedRoof && inheritedFactory && inheritedPlain,
        "NextParticle inherits its own birth geometry after producer landing and source removal");

    // Compare identical standalone emissions: only the named presentation field changes.
    RuntimeParticleSystemState reference(15);
    if (!expect(reference.spawnParticle(rules, "Plume", birth) != 0,
            "presentation revision reference")) return false;
    const auto referenceSnapshot = reference.renderSnapshots(rules).front();
    const auto verifyRevision = [&](const RuntimeParticleSpawnContext& changed, const std::string& name,
        bool changesAuthority) {
        RuntimeParticleSystemState variant(15);
        if (!expect(variant.spawnParticle(rules, "Plume", changed) != 0,
                "valid revision case: " + name)) return false;
        const auto snapshot = variant.renderSnapshots(rules).front();
        return expect(snapshot.revision != referenceSnapshot.revision
                && (variant.stateHash() != reference.stateHash()) == changesAuthority
                && savedText(variant) != savedText(reference)
                && snapshot.rawX == referenceSnapshot.rawX && snapshot.rawY == referenceSnapshot.rawY
                && snapshot.rawZ == referenceSnapshot.rawZ
                && snapshot.presentation == changed.presentation
                && snapshot.spriteAttachment == changed.spriteAttachment,
            "presentation revision/hash must include " + name + " without moving raw coordinates");
    };
    const auto revisionCase = [&](const std::string& name, auto mutate, bool changesAuthority = true) {
        auto changed = birth;
        mutate(changed);
        return verifyRevision(changed, name, changesAuthority);
    };
    ok &= revisionCase("source cell sum", [](auto& c) { c.presentation.sourceCellSum += .125; });
    ok &= revisionCase("source level", [](auto& c) { c.presentation.sourceLevel += .25; });
    ok &= revisionCase("factory cancellation", [](auto& c) { c.presentation.factoryLiftCancellation = 0; });
    ok &= revisionCase("roof count", [](auto& c) { c.presentation.roofs.push_back({}); });
    ok &= revisionCase("roof cell sum", [](auto& c) { c.presentation.roofs[0].cellSum += .125; });
    ok &= revisionCase("roof level", [](auto& c) { c.presentation.roofs[0].level += .25; });
    ok &= revisionCase("second roof level", [](auto& c) { c.presentation.roofs[1].level += .25; });
    ok &= revisionCase("authored height", [](auto& c) { ++c.presentation.roofs[0].authoredHeight; });
    ok &= revisionCase("native sprite height", [](auto& c) { ++c.presentation.roofs[0].nativeSpriteHeight; }, false);
    ok &= revisionCase("airport clearance fraction", [](auto& c) { c.presentation.clearanceFraction = .5; }, false);
    ok &= revisionCase("managed Kirov clearance", [](auto& c) { c.presentation.resolvedClearanceLevel = 1.25; }, false);
    ok &= revisionCase("rubble", [](auto& c) { c.presentation.roofs[0].rubble = true; });
    ok &= revisionCase("screen X", [](auto& c) { ++c.spriteAttachment.screenOffsetX; });
    ok &= revisionCase("screen Y", [](auto& c) { ++c.spriteAttachment.screenOffsetY; });
    ok &= revisionCase("native X", [](auto& c) { ++c.spriteAttachment.nativeFixedX; });
    ok &= revisionCase("native Y", [](auto& c) { ++c.spriteAttachment.nativeFixedY; });
    ok &= revisionCase("voxel anchor", [](auto& c) { c.spriteAttachment.voxelAnchor = false; });
    ok &= revisionCase("attachment airborne", [](auto& c) { c.spriteAttachment.airborne = false; });
    auto plain = birth;
    plain.presentation = {};
    plain.spriteAttachment = {};
    plain.screenOffsetY = 0;
    RuntimeParticleSystemState disabled(15), enabled(15);
    (void)disabled.spawnParticle(rules, "Plume", plain);
    auto emptyEnabled = plain;
    emptyEnabled.presentation.enabled = true;
    (void)enabled.spawnParticle(rules, "Plume", emptyEnabled);
    ok &= expect(disabled.renderSnapshots(rules).front().revision != enabled.renderSnapshots(rules).front().revision
            && disabled.stateHash() != enabled.stateHash(),
        "presentation enabled alone changes revision and hash");
    auto missile = plain;
    missile.spriteAttachment.airborne = true;
    RuntimeParticleSystemState missileSmoke(15), ordinarySmoke(15);
    (void)missileSmoke.spawnParticle(rules, "Plume", missile);
    missile.sourceTag = "ordinary";
    (void)ordinarySmoke.spawnParticle(rules, "Plume", missile);
    ok &= expect(missileSmoke.renderSnapshots(rules).front().airborneDamageSmoke
            && !ordinarySmoke.renderSnapshots(rules).front().airborneDamageSmoke
            && !disabled.renderSnapshots(rules).front().airborneDamageSmoke,
        "airborne sprite attachment enters the damage smoke air layer with zero legacy offset only for damage smoke");

    // Native SHP dimensions are local presentation, like the pixels in that
    // sprite. Different/missing assets must not split otherwise equal peers.
    // Exercise producers, already emitted puffs, NextParticle and resumed saves.
    for (int hz : {15, 30, 60}) for (int localHeight : {0, 213}) {
        RuntimeParticleSystemState peerA(hz), peerB(hz);
        auto a = birth;
        a.presentation.roofs[1].rubble = false;
        auto b = a;
        b.presentation.roofs[1].nativeSpriteHeight = localHeight;
        const auto idA = peerA.spawnSystem(rules, "Outlet", a);
        const auto idB = peerB.spawnSystem(rules, "Outlet", b);
        if (!expect(idA != 0 && idA == idB, "local-height peer producers")) return false;
        int differingRevisions = 0, chainedPairs = 0;
        for (int tick = 0; tick < hz * 2; ++tick) {
            if (tick == hz / 5) {
                const RuntimeParticlePoint moved{2048.0, 3072.0, 800.0};
                ok &= expect(peerA.moveSystem(idA, moved, a.owner, a.screenOffsetY, a.presentation, a.spriteAttachment)
                        && peerB.moveSystem(idB, moved, b.owner, b.screenOffsetY, b.presentation, b.spriteAttachment),
                    "local-height peers move their producer");
            }
            if (tick == hz / 2) {
                ok &= expect(peerA.stopSystemsForSource(a.sourceObjectId) == 1U
                        && peerB.stopSystemsForSource(b.sourceObjectId) == 1U,
                    "local-height peer source removal");
            }
            const auto eventsA = peerA.tick(rules);
            const auto eventsB = peerB.tick(rules);
            const auto framesA = peerA.renderSnapshots(rules);
            const auto framesB = peerB.renderSnapshots(rules);
            bool identicalSimulation = peerA.particles().size() == peerB.particles().size();
            for (std::size_t index = 0; identicalSimulation && index < peerA.particles().size(); ++index) {
                const auto& left = peerA.particles()[index];
                const auto& right = peerB.particles()[index];
                identicalSimulation = left.id == right.id && left.typeId == right.typeId
                    && left.systemInstanceId == right.systemInstanceId
                    && samePoint(left.position, right.position) && samePoint(left.velocity, right.velocity)
                    && left.ageTicks == right.ageTicks && left.remainingLifetime == right.remainingLifetime
                    && left.speed == right.speed && left.stateAI == right.stateAI
                    && left.rngState == right.rngState;
                if (left.typeId == "After") ++chainedPairs;
            }
            ok &= expect(identicalSimulation && sameEvents(eventsA, eventsB)
                    && peerA.stateHash() == peerB.stateHash() && framesA.size() == framesB.size(),
                "local native heights must not change lockstep simulation at " + std::to_string(hz)
                    + " Hz tick " + std::to_string(tick));
            if (!framesA.empty() && framesA.size() == framesB.size()) {
                ok &= expect(framesA.front().revision != framesB.front().revision
                        && framesA.front().presentation.roofs.size() == 2U
                        && framesB.front().presentation.roofs.size() == 2U
                        && framesA.front().presentation.roofs[1].nativeSpriteHeight == 97
                        && framesB.front().presentation.roofs[1].nativeSpriteHeight == localHeight
                        && savedText(peerA) != savedText(peerB),
                    "local native height remains in complete saves and render revisions");
                ++differingRevisions;
            }
            if (tick == hz / 3) {
                const std::string beforeSave = savedText(peerB);
                RuntimeParticleSystemState resumed;
                IniFile resumedArchive;
                ok &= expect(resumedArchive.loadFromString(beforeSave) && resumed.load(resumedArchive, rules)
                        && savedText(resumed) == beforeSave && resumed.stateHash() == peerA.stateHash()
                        && sameSnapshots(resumed.renderSnapshots(rules), framesB),
                    "local native height survives reload without becoming authoritative");
                peerB = std::move(resumed);
            }
        }
        ok &= expect(differingRevisions > 0 && chainedPairs > 0,
            "local-height lockstep fixture exercised visible and chained smoke");
    }

    // A descent fade freezes with all 64 real roofs. It changes pixels/revisions
    // without changing particle simulation, including future and chained puffs.
    auto fullRoofBirth = birth;
    fullRoofBirth.presentation.roofs.clear();
    for (std::size_t index = 0; index < runtimeParticleMaximumClearanceRoofs; ++index) {
        fullRoofBirth.presentation.roofs.push_back({
            12.0 + static_cast<double>(index) / 8.0,
            static_cast<double>(index % 4U) / 4.0,
            static_cast<int>(index % 7U) + 1, 0, false});
    }
    for (int hz : {15, 30, 60}) {
        const std::array<double, 3> fractions{0.0, .5, 1.0};
        std::array<RuntimeParticleSystemState, 3> peers{
            RuntimeParticleSystemState(hz), RuntimeParticleSystemState(hz), RuntimeParticleSystemState(hz)};
        std::array<RuntimeParticleSystemState, 3> resumed;
        for (std::size_t index = 0; index < peers.size(); ++index) {
            auto context = fullRoofBirth;
            context.presentation.clearanceFraction = fractions[index];
            if (!expect(peers[index].spawnSystem(rules, "Outlet", context) != 0,
                    "64-roof clearance fraction producer")) return false;
            (void)peers[index].tick(rules);
            if (!expect(peers[index].particles().size() == 1U,
                    "clearance fraction producer emitted a puff")) return false;
            const auto save = savedText(peers[index]);
            const auto snapshots = peers[index].renderSnapshots(rules);
            IniFile archive;
            ok &= expect(archive.loadFromString(save) && resumed[index].load(archive, rules)
                    && savedText(resumed[index]) == save
                    && resumed[index].stateHash() == peers[index].stateHash()
                    && sameSnapshots(resumed[index].renderSnapshots(rules), snapshots)
                    && peers[index].systems().front().presentation == context.presentation
                    && peers[index].particles().front().presentation == context.presentation
                    && snapshots.front().presentation == context.presentation
                    && context.presentation.roofs.size() == 64U
                    && (save.find("PresentationClearanceFraction=") != std::string::npos)
                        == (fractions[index] != 1.0),
                "fraction 0/half/1 preserves every real roof and optional INI representation");
        }
        const auto whole = peers[2].renderSnapshots(rules).front();
        ok &= expect(peers[0].stateHash() == peers[1].stateHash()
                && peers[1].stateHash() == peers[2].stateHash()
                && peers[0].renderSnapshots(rules).front().revision != whole.revision
                && peers[1].renderSnapshots(rules).front().revision != whole.revision
                && peers[0].renderSnapshots(rules).front().revision
                    != peers[1].renderSnapshots(rules).front().revision,
            "clearance fraction changes render revisions while preserving authority");
        bool inheritedFraction = false;
        for (int tick = 0; tick < hz * 2; ++tick) {
            const auto referenceEvents = peers[2].tick(rules);
            for (std::size_t index = 0; index < peers.size(); ++index) {
                const auto events = index == 2U ? referenceEvents : peers[index].tick(rules);
                const auto restoredEvents = resumed[index].tick(rules);
                ok &= expect(sameEvents(events, referenceEvents) && sameEvents(events, restoredEvents)
                        && peers[index].stateHash() == peers[2].stateHash()
                        && resumed[index].stateHash() == peers[index].stateHash()
                        && sameSnapshots(peers[index].renderSnapshots(rules), resumed[index].renderSnapshots(rules))
                        && std::all_of(peers[index].particles().begin(), peers[index].particles().end(),
                            [&](const auto& particle) {
                                return particle.presentation.clearanceFraction == fractions[index]
                                    && particle.presentation.roofs == fullRoofBirth.presentation.roofs;
                            }),
                    "clearance fraction frozen replay at " + std::to_string(hz)
                        + " Hz tick " + std::to_string(tick));
                inheritedFraction |= index == 0U && std::any_of(peers[index].particles().begin(),
                    peers[index].particles().end(), [](const auto& particle) { return particle.typeId == "After"; });
            }
        }
        ok &= expect(inheritedFraction, "NextParticle inherits the fractional 64-roof profile");
    }

    RuntimeParticleSystemState complete(15);
    const auto completeId = complete.spawnSystem(rules, "Outlet", birth);
    (void)complete.tick(rules);
    if (!expect(completeId != 0 && complete.particles().size() == 1U,
            "presentation archive contains both producer and emitted puff")) return false;
    const std::string completeSave = savedText(complete);
    RuntimeParticleSystemState emitting = complete, emittingRestored;
    IniFile emittingArchive;
    if (!expect(emittingArchive.loadFromString(completeSave) && emittingRestored.load(emittingArchive, rules),
            "active producer presentation archive")) return false;
    for (int tick = 0; tick < 18; ++tick) {
        const auto expectedEvents = emitting.tick(rules);
        const auto actualEvents = emittingRestored.tick(rules);
        ok &= expect(sameEvents(expectedEvents, actualEvents)
                && emitting.stateHash() == emittingRestored.stateHash()
                && sameSnapshots(emitting.renderSnapshots(rules), emittingRestored.renderSnapshots(rules))
                && std::all_of(emittingRestored.particles().begin(), emittingRestored.particles().end(),
                    [&](const auto& particle) { return particle.presentation == birth.presentation
                        && particle.spriteAttachment == birth.spriteAttachment; }),
            "restored active producer emits and chains the same presentation at tick " + std::to_string(tick));
    }
    // Remove optional fields from both records, exactly as an older Version 1 archive lacks them.
    std::istringstream input(completeSave);
    std::ostringstream legacy;
    for (std::string line; std::getline(input, line);) {
        if (line.rfind("Presentation", 0) != 0 && line.rfind("Attachment", 0) != 0)
            legacy << line << '\n';
    }
    RuntimeParticleSystemState legacyRestored(15), legacyExpected(15);
    auto legacyBirth = birth;
    legacyBirth.presentation = {};
    legacyBirth.spriteAttachment = {};
    (void)legacyExpected.spawnSystem(rules, "Outlet", legacyBirth);
    (void)legacyExpected.tick(rules);
    IniFile legacyArchive;
    ok &= expect(legacyArchive.loadFromString(legacy.str()) && legacyRestored.load(legacyArchive, rules)
            && legacyRestored.stateHash() == legacyExpected.stateHash()
            && sameSnapshots(legacyRestored.renderSnapshots(rules), legacyExpected.renderSnapshots(rules)),
        "Version 1 archives lacking presentation and attachment fields retain exact legacy defaults");

    // Patch a specific record; producer and emitted puff must independently reject malformed data.
    const auto corruptField = [](std::string& text, const std::string& section,
        const std::string& field, const std::string& value) {
        const auto start = text.find("[ParticleRuntime." + section + ".0]\n");
        if (start == std::string::npos) return false;
        const auto end = text.find("\n[", start + 1);
        const auto position = text.find("\n" + field + "=", start);
        if (position == std::string::npos || (end != std::string::npos && position >= end)) return false;
        const auto lineEnd = text.find('\n', position + 1);
        text.replace(position + 1, lineEnd - position - 1, field + "=" + value);
        return true;
    };
    auto fractionalBirth = birth;
    fractionalBirth.presentation.clearanceFraction = .5;
    fractionalBirth.presentation.resolvedClearanceLevel = 1.25;
    RuntimeParticleSystemState fractional(15);
    if (!expect(fractional.spawnSystem(rules, "Outlet", fractionalBirth) != 0,
            "malformed clearance fraction fixture producer")) return false;
    (void)fractional.tick(rules);
    const std::string fractionalSave = savedText(fractional);
    {
        IniFile archive;
        RuntimeParticleSystemState restored;
        ok &= expect(archive.loadFromString(fractionalSave) && restored.load(archive, rules)
                && restored.stateHash() == fractional.stateHash()
                && sameSnapshots(restored.renderSnapshots(rules), fractional.renderSnapshots(rules)),
            "managed Kirov frozen lift roundtrip preserves rendering without changing authority");
        for (const auto* section : {"System", "Particle"}) {
            for (const auto* value : {"-2", "-0.5", "64.01", "nan", "infinity"}) {
                auto corrupted = fractionalSave;
                IniFile invalid;
                std::string error;
                ok &= expect(corruptField(corrupted, section, "PresentationResolvedClearanceLevel", value)
                        && invalid.loadFromString(corrupted) && !restored.load(invalid, rules, &error)
                        && savedText(restored) == fractionalSave && !error.empty(),
                    std::string("atomic malformed ") + section + " managed clearance=" + value);
            }
        }
    }
    for (const auto* section : {"System", "Particle"}) {
        for (const auto* value : {"-0.01", "1.01", "nan", "infinity"}) {
            auto corrupted = fractionalSave;
            IniFile archive;
            std::string loadError;
            const auto before = fractional.stateHash();
            const auto snapshots = fractional.renderSnapshots(rules);
            ok &= expect(corruptField(corrupted, section, "PresentationClearanceFraction", value)
                    && archive.loadFromString(corrupted) && !fractional.load(archive, rules, &loadError)
                    && !loadError.empty() && fractional.stateHash() == before
                    && savedText(fractional) == fractionalSave
                    && sameSnapshots(fractional.renderSnapshots(rules), snapshots),
                std::string("atomic malformed ") + section + " clearance fraction=" + value);
        }
    }
    std::istringstream fractionalInput(fractionalSave);
    std::ostringstream olderFractionSave;
    for (std::string line; std::getline(fractionalInput, line);) {
        if (line.rfind("PresentationClearanceFraction=", 0) != 0
            && line.rfind("PresentationResolvedClearanceLevel=", 0) != 0) olderFractionSave << line << '\n';
    }
    IniFile olderFractionArchive;
    RuntimeParticleSystemState olderFractionRestored;
    ok &= expect(olderFractionArchive.loadFromString(olderFractionSave.str())
            && olderFractionRestored.load(olderFractionArchive, rules)
            && olderFractionRestored.stateHash() == fractional.stateHash()
            && std::all_of(olderFractionRestored.systems().begin(), olderFractionRestored.systems().end(),
                [](const auto& system) { return system.presentation.clearanceFraction == 1.0
                    && system.presentation.resolvedClearanceLevel == -1.0; })
            && std::all_of(olderFractionRestored.particles().begin(), olderFractionRestored.particles().end(),
                [](const auto& particle) { return particle.presentation.clearanceFraction == 1.0
                    && particle.presentation.resolvedClearanceLevel == -1.0; })
            && olderFractionRestored.renderSnapshots(rules).front().revision
                != fractional.renderSnapshots(rules).front().revision,
        "older enabled profiles lacking the optional clearance fraction retain their full roof lift");
    const std::vector<std::pair<std::string, std::string>> invalidFields{
        {"PresentationEnabled", "perhaps"}, {"PresentationEnabled", "no"},
        {"PresentationSourceCellSum", "nan"}, {"PresentationSourceCellSum", "inf"},
        {"PresentationSourceCellSum", "2097153"}, {"PresentationSourceCellSum", "-2097153"},
        {"PresentationSourceLevel", "nan"}, {"PresentationSourceLevel", "1048577"},
        {"PresentationSourceLevel", "-1048577"},
        {"PresentationFactoryLiftCancellation", "-2"}, {"PresentationFactoryLiftCancellation", "35"},
        {"PresentationFactoryLiftCancellation", "1.5"},
        {"PresentationRoofCount", "-1"}, {"PresentationRoofCount", "65"},
        {"PresentationRoofCount", "2147483648"}, {"PresentationRoofCount", "3"},
        {"PresentationRoof0CellSum", "nan"}, {"PresentationRoof0CellSum", "2097153"},
        {"PresentationRoof0Level", "-inf"}, {"PresentationRoof0Level", "1048577"},
        {"PresentationRoof0AuthoredHeight", "-1"}, {"PresentationRoof0AuthoredHeight", "65537"},
        {"PresentationRoof0NativeSpriteHeight", "-1"}, {"PresentationRoof0NativeSpriteHeight", "65537"},
        {"PresentationRoof0Rubble", "perhaps"}, {"PresentationRoof0Rubble", ""},
        {"AttachmentScreenOffsetX", "4097"}, {"AttachmentScreenOffsetX", "-4097"},
        {"AttachmentScreenOffsetY", "4097"}, {"AttachmentScreenOffsetY", "-4097"},
        {"AttachmentNativeFixedX", "4097"}, {"AttachmentNativeFixedX", "-4097"},
        {"AttachmentNativeFixedY", "4097"}, {"AttachmentNativeFixedY", "-4097"},
        {"AttachmentNativeFixedX", "1.5"}, {"AttachmentVoxelAnchor", "perhaps"},
        {"AttachmentAirborne", "perhaps"}
    };
    for (const auto* section : {"System", "Particle"}) {
        for (const char* field : {"PresentationSourceCellSum", "PresentationSourceLevel",
                "PresentationFactoryLiftCancellation", "PresentationRoofCount",
                "AttachmentScreenOffsetX", "AttachmentScreenOffsetY", "AttachmentNativeFixedX",
                "AttachmentNativeFixedY", "AttachmentVoxelAnchor", "AttachmentAirborne"}) {
            std::string corrupted = completeSave;
            const auto sectionStart = corrupted.find(std::string("[ParticleRuntime.") + section + ".0]\n");
            const auto fieldStart = corrupted.find(std::string("\n") + field + "=", sectionStart);
            const auto fieldEnd = corrupted.find('\n', fieldStart + 1);
            ok &= expect(sectionStart != std::string::npos && fieldStart != std::string::npos
                && fieldEnd != std::string::npos, "missing field fixture");
            if (fieldStart == std::string::npos || fieldEnd == std::string::npos) continue;
            corrupted.erase(fieldStart + 1, fieldEnd - fieldStart);
            IniFile archive;
            const auto before = complete.stateHash();
            std::string error;
            ok &= expect(archive.loadFromString(corrupted) && !complete.load(archive, rules, &error)
                    && !error.empty() && complete.stateHash() == before && savedText(complete) == completeSave,
                std::string("active presentation requires complete ") + section + " field " + field);
        }
    }
    for (const auto* section : {"System", "Particle"}) {
        for (const auto& [field, value] : invalidFields) {
            std::string corrupted = completeSave;
            IniFile corruptArchive;
            std::string error;
            const auto before = complete.stateHash();
            const auto snapshots = complete.renderSnapshots(rules);
            ok &= expect(corruptField(corrupted, section, field, value)
                    && corruptArchive.loadFromString(corrupted)
                    && !complete.load(corruptArchive, rules, &error) && !error.empty()
                    && complete.stateHash() == before && savedText(complete) == completeSave
                    && sameSnapshots(complete.renderSnapshots(rules), snapshots),
                std::string("atomic malformed ") + section + " rejection: " + field + "=" + value);
        }
    }
    const auto rejectLive = [&](const std::string& name, auto mutate) {
        auto invalid = birth;
        mutate(invalid);
        std::string systemError, particleError;
        const auto before = complete.stateHash();
        const bool rejectedSystem = complete.spawnSystem(rules, "Outlet", invalid, &systemError) == 0;
        const bool rejectedParticle = complete.spawnParticle(rules, "Plume", invalid, &particleError) == 0;
        const bool rejectedMove = !complete.moveSystem(completeId, {999.0, 888.0, 777.0}, "changed", 0,
            invalid.presentation, invalid.spriteAttachment);
        return expect(rejectedSystem && rejectedParticle && rejectedMove
                && !systemError.empty() && !particleError.empty()
                && complete.stateHash() == before && savedText(complete) == completeSave,
            "spawn/particle/move atomically reject " + name);
    };
    ok &= rejectLive("disabled non-default geometry", [](auto& c) { c.presentation.enabled = false; });
    ok &= rejectLive("too many roofs", [](auto& c) { c.presentation.roofs.resize(65); });
    ok &= rejectLive("NaN source sum", [](auto& c) { c.presentation.sourceCellSum = std::numeric_limits<double>::quiet_NaN(); });
    ok &= rejectLive("infinite source level", [](auto& c) { c.presentation.sourceLevel = std::numeric_limits<double>::infinity(); });
    ok &= rejectLive("source sum range", [](auto& c) { c.presentation.sourceCellSum = 2097153.0; });
    ok &= rejectLive("source level range", [](auto& c) { c.presentation.sourceLevel = -1048577.0; });
    ok &= rejectLive("NaN roof sum", [](auto& c) { c.presentation.roofs[0].cellSum = std::numeric_limits<double>::quiet_NaN(); });
    ok &= rejectLive("infinite roof level", [](auto& c) { c.presentation.roofs[0].level = -std::numeric_limits<double>::infinity(); });
    ok &= rejectLive("roof sum range", [](auto& c) { c.presentation.roofs[0].cellSum = -2097153.0; });
    ok &= rejectLive("roof level range", [](auto& c) { c.presentation.roofs[0].level = 1048577.0; });
    ok &= rejectLive("factory below sentinel", [](auto& c) { c.presentation.factoryLiftCancellation = -2; });
    ok &= rejectLive("factory upper bound", [](auto& c) { c.presentation.factoryLiftCancellation = 35; });
    ok &= rejectLive("negative clearance fraction", [](auto& c) { c.presentation.clearanceFraction = -.01; });
    ok &= rejectLive("invalid negative managed clearance", [](auto& c) { c.presentation.resolvedClearanceLevel = -.5; });
    ok &= rejectLive("oversized managed clearance", [](auto& c) { c.presentation.resolvedClearanceLevel = 64.01; });
    ok &= rejectLive("NaN managed clearance", [](auto& c) {
        c.presentation.resolvedClearanceLevel = std::numeric_limits<double>::quiet_NaN(); });
    ok &= rejectLive("oversized clearance fraction", [](auto& c) { c.presentation.clearanceFraction = 1.01; });
    ok &= rejectLive("NaN clearance fraction", [](auto& c) {
        c.presentation.clearanceFraction = std::numeric_limits<double>::quiet_NaN(); });
    ok &= rejectLive("infinite clearance fraction", [](auto& c) {
        c.presentation.clearanceFraction = std::numeric_limits<double>::infinity(); });
    ok &= rejectLive("negative authored height", [](auto& c) { c.presentation.roofs[0].authoredHeight = -1; });
    ok &= rejectLive("oversized authored height", [](auto& c) { c.presentation.roofs[0].authoredHeight = 65537; });
    ok &= rejectLive("negative native height", [](auto& c) { c.presentation.roofs[0].nativeSpriteHeight = -1; });
    ok &= rejectLive("oversized native height", [](auto& c) { c.presentation.roofs[0].nativeSpriteHeight = 65537; });
    for (int value : {-4097, 4097}) {
        ok &= rejectLive("attachment screen X", [value](auto& c) { c.spriteAttachment.screenOffsetX = value; });
        ok &= rejectLive("attachment screen Y", [value](auto& c) { c.spriteAttachment.screenOffsetY = value; });
        ok &= rejectLive("attachment native X", [value](auto& c) { c.spriteAttachment.nativeFixedX = value; });
        ok &= rejectLive("attachment native Y", [value](auto& c) { c.spriteAttachment.nativeFixedY = value; });
    }
    auto boundary = birth;
    boundary.presentation.sourceCellSum = 2097152.0;
    boundary.presentation.sourceLevel = -1048576.0;
    boundary.presentation.factoryLiftCancellation = 34;
    boundary.presentation.roofs.assign(64, {-2097152.0, 1048576.0, 65536, 65536, true});
    boundary.spriteAttachment = {-4096, 4096, 4096, -4096, true, true};
    RuntimeParticleSystemState bounded(15), boundedRestored;
    const auto boundedId = bounded.spawnSystem(rules, "Outlet", boundary);
    IniFile boundedArchive;
    ok &= expect(boundedId != 0 && bounded.spawnParticle(rules, "Plume", boundary) != 0
            && bounded.moveSystem(boundedId, boundary.position, boundary.owner, boundary.screenOffsetY,
                boundary.presentation, boundary.spriteAttachment)
            && boundedArchive.loadFromString(savedText(bounded)) && boundedRestored.load(boundedArchive, rules)
            && boundedRestored.stateHash() == bounded.stateHash(),
        "all documented inclusive presentation/attachment bounds remain valid through spawn, move and load");
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--benchmark") {
        return runPerformanceBenchmark();
    }
    if (argc != 3 && argc != 5) {
        std::cerr << "usage: concord_particle_system_smoke <rules.ini> <art.ini>"
                  << " [stock-rules.ini stock-art.ini]\n";
        return 2;
    }

    IniFile rulesIni;
    IniFile artIni;
    if (!rulesIni.loadFromFile(argv[1]) || !artIni.loadFromFile(argv[2])) {
        std::cerr << "particle smoke: failed to load fixture\n";
        return 3;
    }
    RulesDatabase rules;
    ArtDatabase art;
    if (!rules.loadFromIni(rulesIni) || !art.loadFromIni(artIni)) {
        std::cerr << "particle smoke: database load failed\n";
        return 4;
    }

    bool ok = verifySnapshotOrderAfterLoad(rules);
    ok &= verifyLongRunningParticleCleanup();
    ok &= verifyEmissionScreenOffset();
    ok &= verifyEmissionPresentation();
    ok &= expect(
        rules.getParticleSystemTypes().size() == 6U,
        "ParticleSystems catalog count"
    );
    ok &= expect(
        rules.getParticleTypes().size() == 13U,
        "Particles catalog count"
    );
    ok &= expect(
        rules.getInfantryVirusAnim() == "VIRUSD",
        "[General] InfantryVirus animation"
    );
    ok &= expect(
        rules.getWindDirection() == 3,
        "[General] WindDirection normalizes to one of eight directions"
    );
    const ParticleSystemType* smokeType =
        rules.findParticleSystemType("smokesys");
    ok &= expect(
        smokeType != nullptr
            && smokeType->behavior == ParticleBehavior::Smoke
            && smokeType->spawns
            && smokeType->spawnFrames == 2
            && smokeType->particleCap == 3
            && smokeType->spawnRadius == 2
            && smokeType->slowdown == 0.125
            && smokeType->spawnDirection.z == 1.0,
        "Smoke system fields"
    );
    const ParticleSystemType* sparkSystem =
        rules.findParticleSystemType("SparkSys");
    ok &= expect(
        sparkSystem != nullptr
            && sparkSystem->behavior == ParticleBehavior::Spark
            && sparkSystem->sparkSpawnFrames == 1
            && sparkSystem->spawnSparkPercentage == 1.0
            && sparkSystem->lightSize == 15
            && sparkSystem->hasLaserColor
            && sparkSystem->laserColor.blue == 255
            && sparkSystem->laser
            && sparkSystem->oneFrameLight,
        "Spark system fields"
    );
    const ParticleType* sparkParticle = rules.findParticleType("SparkParticle");
    ok &= expect(
        sparkParticle != nullptr
            && sparkParticle->colorList.size() == 3U
            && sparkParticle->hasStartColor1
            && sparkParticle->hasStartColor2
            && sparkParticle->colorSpeed == 0.13
            && sparkParticle->zVelocityRange == 15,
        "Particle color and velocity fields"
    );
    const ParticleSystemType* railgunSystem =
        rules.findParticleSystemType("RailgunSys");
    ok &= expect(
        railgunSystem != nullptr
            && railgunSystem->behavior == ParticleBehavior::Railgun
            && railgunSystem->particlesPerCoord == 0.25
            && railgunSystem->spiralDeltaPerCoord == 0.25
            && railgunSystem->spiralRadius == 4.0
            && railgunSystem->particleCap == 8
            && railgunSystem->laser
            && railgunSystem->hasLaserColor
            && railgunSystem->lightSize == 24
            && !railgunSystem->oneFrameLight,
        "Railgun line, spiral, beam, and local-light fields"
    );
    const ParticleSystemType* cutoffSmokeSystem =
        rules.findParticleSystemType("CutoffSmokeSys");
    ok &= expect(
        cutoffSmokeSystem != nullptr
            && cutoffSmokeSystem->spawnCutoff == 2.0
            && cutoffSmokeSystem->spawnTranslucencyCutoff == 2.0
            && cutoffSmokeSystem->slowdown == 1.0,
        "Smoke cutoff fields"
    );
    const WeaponType* fixtureRailWeapon =
        rules.findWeaponType("RailParticleWeapon");
    const WeaponType* fixtureFireWeapon =
        rules.findWeaponType("FireParticleWeapon");
    ok &= expect(
        fixtureRailWeapon != nullptr && fixtureRailWeapon->isRailgun
            && fixtureRailWeapon->attachedParticleSystem == "RailgunSys"
            && fixtureFireWeapon != nullptr
            && fixtureFireWeapon->useFireParticles
            && fixtureFireWeapon->attachedParticleSystem == "SmokeSys",
        "weapon attached-particle fields"
    );
    const ParticleType* virusParticle = rules.findParticleType("VirusCloud1");
    ok &= expect(
        virusParticle != nullptr
            && virusParticle->behavior == ParticleBehavior::Gas
            && virusParticle->maxDamageDelay == 2
            && virusParticle->maxLifetime == 8
            && virusParticle->numLoopFrames == 3
            && virusParticle->translucent25State == 1
            && virusParticle->translucent50State == 2
            && virusParticle->nextParticle == "VirusCloudD1"
            && virusParticle->nextParticleOffset.z == 150.0,
        "Virus gas fields"
    );
    const WarheadType* smokeSpawner = rules.findWarheadType("SmokeSpawner");
    ok &= expect(
        smokeSpawner != nullptr
            && smokeSpawner->particleSystem == "SmokeSys",
        "explicit Warhead ParticleSystem field"
    );
    const ArtType* virusDeath = art.findArtType("virusd");
    ok &= expect(
        virusDeath != nullptr
            && virusDeath->scorch
            && virusDeath->spawnsParticle == "VirusCloud1"
            && virusDeath->numParticles == 3,
        "VIRUSD SpawnsParticle and NumParticles"
    );
    if (!ok || virusDeath == nullptr) {
        return 5;
    }

    if (argc == 5) {
        IniFile stockRulesIni;
        IniFile stockArtIni;
        RulesDatabase stockRules;
        ArtDatabase stockArt;
        ok &= expect(
            stockRulesIni.loadFromFile(argv[3])
                && stockArtIni.loadFromFile(argv[4])
                && stockRules.loadFromIni(stockRulesIni)
                && stockArt.loadFromIni(stockArtIni),
            "stock rulesmd/artmd load"
        );
        const ParticleSystemType* stockGas =
            stockRules.findParticleSystemType("GasCloudSys");
        const ParticleSystemType* stockSmoke =
            stockRules.findParticleSystemType("BigGreySmokeSys");
        const ParticleSystemType* stockRailgun =
            stockRules.findParticleSystemType("SmallRailgunSys");
        const ParticleType* stockVirus =
            stockRules.findParticleType("VirusCloud1");
        const WeaponType* stockRepairWeapon =
            stockRules.findWeaponType("RepairBullet");
        const ArtType* stockVirusDeath = stockArt.findArtType("VIRUSD");
        const ArtType* stockExplosion = stockArt.findArtType("TWLT070");
        const ArtType* stockNukeBall = stockArt.findArtType("NUKEBALL");
        const ArtType* stockElectro = stockArt.findArtType("ELECTRO");
        ok &= expect(
            stockRules.getParticleSystemTypes().size() == 13U
                && stockRules.getParticleTypes().size() == 22U
                && stockGas != nullptr
                && stockGas->behavior == ParticleBehavior::Gas
                && stockGas->holdsWhat == "GasCloud1"
                && stockSmoke != nullptr
                && stockSmoke->behavior == ParticleBehavior::Smoke
                && stockSmoke->spawns
                && stockSmoke->spawnFrames == 10
                && stockVirus != nullptr
                && stockVirus->maxDamageDelay == 30
                && stockVirus->maxLifetime == 1000
                && stockVirus->warhead == "VirusGas"
                && stockVirus->nextParticle == "VirusCloudD1"
                && stockRules.getInfantryVirusAnim() == "VIRUSD"
                && stockVirusDeath != nullptr
                && stockVirusDeath->scorch
                && stockVirusDeath->spawnsParticle == "VirusCloud1"
                && stockVirusDeath->numParticles == 3
                && stockExplosion != nullptr
                && stockExplosion->crater
                && stockExplosion->scorch
                && stockNukeBall != nullptr
                && stockNukeBall->crater
                && stockNukeBall->forceBigCraters
                && stockElectro != nullptr
                && stockElectro->scorch
                && stockElectro->hasAnimationLoopEnd
                && stockElectro->animationLoopEnd == 3
                && stockElectro->animationLoopCount == 5,
            "stock ParticleSystems/Particles and VIRUSD semantics"
        );
        ok &= expect(
            stockRailgun != nullptr
                && stockRailgun->behavior == ParticleBehavior::Railgun
                && near(stockRailgun->particlesPerCoord, 0.1)
                && near(stockRailgun->spiralDeltaPerCoord, 0.035)
                && near(stockRailgun->spiralRadius, 6.0)
                && stockRailgun->laser,
            "stock Railgun particle-system semantics"
        );
        ok &= expect(
            stockRepairWeapon != nullptr
                && stockRepairWeapon->useSparkParticles
                && stockRepairWeapon->attachedParticleSystem == "WeldingSys",
            "stock repair weapon AttachedParticleSystem semantics"
        );
        for (int hz : {15, 30, 60}) {
            RuntimeParticleSystemState greySmoke(hz);
            greySmoke.spawnParticle(stockRules, "SmallGreySmoke", virusContext());
            std::vector<int> frames;
            bool originalStrip = true;
            for (int tick = 0; tick <= 80 * hz / 15; ++tick) {
                const auto snapshots = greySmoke.renderSnapshots(stockRules, &stockArt);
                for (const auto& snapshot : snapshots) {
                    originalStrip = originalStrip && snapshot.imageId == "SGRYSMK1"
                        && snapshot.renderKind == RuntimeParticleRenderKind::Shp
                        && snapshot.frameIndex >= 0 && snapshot.frameIndex < 20;
                    if (frames.empty() || frames.back() != snapshot.frameIndex)
                        frames.push_back(snapshot.frameIndex);
                }
                (void)greySmoke.tick(stockRules);
            }
            ok &= expect(originalStrip && frames.size() == 20U && frames.front() == 0
                && frames.back() == 19 && greySmoke.particles().empty(),
                "stock SGRYSMK1 plays all 20 frames once and expires at 15/30/60 Hz");
        }
        if (stockVirusDeath != nullptr) {
            std::set<std::array<int, 6>> directionSets;
            for (int hz : {15, 30, 60}) for (std::uint64_t seed = 0; seed < 128; ++seed) {
                auto input = virusContext(); input.seed = 0xd1e5ULL + seed * 12U;
                RuntimeParticleSystemState first(hz), second(hz);
                first.spawnFromAnimation(stockRules, *stockVirusDeath, input);
                second.spawnFromAnimation(stockRules, *stockVirusDeath, input);
                if (!expect(first.particles().size() == 3 && first.stateHash() == second.stateHash(),
                        "three deterministic virus clouds per rupture")) return 5;
                const auto initial = first.particles();
                std::array<int, 6> directions{};
                for (int i = 0; i < 3; ++i) {
                    const auto& cloud = initial[i];
                    if (!expect(cloud.speed > 1.9 && cloud.speed < 2.1 && cloud.velocity.z == 0,
                            "virus initial drift must be slow (about 0.12 cells/second)")) return 5;
                    directions[i*2] = static_cast<int>(std::lround(cloud.velocity.x * 1000));
                    directions[i*2+1] = static_cast<int>(std::lround(cloud.velocity.y * 1000));
                    for (int j = 0; j < i; ++j) {
                        const double dot = cloud.velocity.x*initial[j].velocity.x
                            + cloud.velocity.y*initial[j].velocity.y;
                        if (!expect(dot/(cloud.speed*initial[j].speed) < 0.6,
                                "virus clouds must spread in three separate directions")) return 5;
                    }
                }
                directionSets.insert(directions);
                for (int tick = 0; tick < hz*2; ++tick) { first.tick(stockRules); second.tick(stockRules); }
                if (!expect(first.stateHash() == second.stateHash(), "virus drift lockstep")) return 5;
                for (const auto& cloud : first.particles()) {
                    const double distance = std::hypot(cloud.position.x-input.position.x, cloud.position.y-input.position.y);
                    if (!expect(distance > 50 && distance < 65, "virus covers about 0.23 cells in two seconds")) return 5;
                }
            }
            if (!expect(directionSets.size() >= 32, "virus directions vary independently across rupture seeds")) return 5;
            std::cout << "Virus drift: 384 seeded/rate cases, distinct triples=" << directionSets.size()
                << ", speed=0.117 cells/s, three separated directions, lockstep passed\n";
            RuntimeParticlePoint reference;
            for (int hz : {15, 30, 60}) {
                RuntimeParticleSystemState cloudState(hz);
                const auto gasContext = virusContext();
                cloudState.spawnFromAnimation(stockRules, *stockVirusDeath, gasContext);
                std::size_t damageEvents = 0;
                for (int tick = 0; tick < hz * 2; ++tick)
                    damageEvents += cloudState.tick(stockRules).size();
                const auto& clouds = cloudState.particles();
                ok &= expect(clouds.size() == 3 && damageEvents == 3,
                    "stock spreading clouds keep authored periodic damage");
                if (clouds.size() != 3) continue;
                const double dx = clouds[0].position.x - gasContext.position.x;
                const double dy = clouds[0].position.y - gasContext.position.y;
                ok &= expect(std::hypot(dx, dy) > 50.0 && std::hypot(dx, dy) < 65.0
                    && std::hypot(clouds[1].position.x - clouds[0].position.x,
                        clouds[1].position.y - clouds[0].position.y) > 50.0,
                    "stock gas disperses in separate directions even with WindEffect=0");
                const auto visual = cloudState.renderSnapshots(stockRules);
                ok &= expect(visual.front().frameIndex > 0, "stock gas SHP advances without NumLoopFrames");
                if (hz == 15) reference = clouds[0].position;
                else ok &= expect(near(clouds[0].position.x, reference.x)
                    && near(clouds[0].position.y, reference.y), "gas diffusion is tick-rate independent");
                std::ostringstream savedGas;
                cloudState.save(savedGas);
                IniFile gasIni;
                RuntimeParticleSystemState resumed(hz);
                std::string gasError;
                ok &= expect(gasIni.loadFromString(savedGas.str())
                    && resumed.load(gasIni, stockRules, &gasError), "moving gas save/load");
                bool faded = false;
                for (int tick = hz * 2; tick < hz * 71; ++tick) {
                    cloudState.tick(stockRules);
                    resumed.tick(stockRules);
                    ok &= expect(cloudState.stateHash() == resumed.stateHash(), "gas replay diverged");
                    const auto snapshots = cloudState.renderSnapshots(stockRules);
                    for (const auto& snapshot : snapshots)
                        faded = faded || (snapshot.particleTypeId == "VirusCloudD1" && snapshot.translucency > 50);
                }
                ok &= expect(faded && cloudState.particles().empty(), "gas dissipation fades and expires");
            }
        }
        if (!ok) {
            return 5;
        }
    }

    RuntimeParticleSystemState virusState;
    const RuntimeParticleSpawnContext context = virusContext();
    const RuntimeParticleSpawnResult virusSpawn = virusState.spawnFromAnimation(
        rules,
        *virusDeath,
        context
    );
    ok &= expect(
        virusSpawn.warnings.empty()
            && virusSpawn.particleInstanceIds.size() == 3U
            && virusState.particles().size() == 3U,
        "VIRUSD creates three authored particles"
    );
    IniFile particleRenderArtIni;
    ArtDatabase particleRenderArt;
    ok &= expect(
        particleRenderArtIni.loadFromString(
            "[TXGASG]\n"
            "Image=TXGASG_RENDER\n"
            "YSortAdjust=17\n"
            "Translucent=yes\n"
        )
            && particleRenderArt.loadFromIni(particleRenderArtIni),
        "particle render art fixture"
    );
    const std::uint64_t hashBeforeSnapshots = virusState.stateHash();
    const std::vector<RuntimeParticleRenderSnapshot> initialSnapshots =
        virusState.renderSnapshots(rules, &particleRenderArt);
    ok &= expect(
        initialSnapshots.size() == 3U
            && initialSnapshots[0].instanceId
                < initialSnapshots[1].instanceId
            && initialSnapshots[1].instanceId
                < initialSnapshots[2].instanceId
            && initialSnapshots[0].particleTypeId == "VirusCloud1"
            && initialSnapshots[0].imageId == "TXGASG_RENDER"
            && initialSnapshots[0].rawX == 12.0
            && initialSnapshots[0].rawY == 24.0
            && initialSnapshots[0].rawZ == 3.0
            && initialSnapshots[0].worldX == 12.0 / 256.0
            && initialSnapshots[0].worldY == 24.0 / 256.0
            && initialSnapshots[0].worldLevel == 3.0 / 256.0
            && initialSnapshots[0].frameIndex == 0
            && initialSnapshots[0].translucency == 50
            && initialSnapshots[0].renderKind
                == RuntimeParticleRenderKind::Shp
            && initialSnapshots[0].ySortAdjust == 17
            && initialSnapshots[0].revision != 0
            && virusState.stateHash() == hashBeforeSnapshots,
        "value-only particle snapshots were unstable or mutated core state"
    );
    std::size_t virusDamageCount = 0;
    for (int tick = 1; tick <= 22; ++tick) {
        const std::vector<RuntimeParticleDamageEvent> events =
            virusState.tick(rules);
        virusDamageCount += events.size();
        for (const RuntimeParticleDamageEvent& event : events) {
            ok &= expect(
                event.warheadId == "VirusGas"
                    && event.sourceObjectId == 77
                    && event.owner == "Yuri;Prime"
                    && event.sourceTag == "VIRUSD/InfDeath=8",
                "VIRUSD damage event preserves attribution"
            );
        }
        if (tick == 4) {
            const std::vector<RuntimeParticleRenderSnapshot> advancedSnapshots =
                virusState.renderSnapshots(rules);
            ok &= expect(
                virusState.particles().size() == 3U
                    && virusState.particles().front().stateAI == 1
                    && advancedSnapshots.size() == 3U
                    && advancedSnapshots.front().frameIndex == 1
                    && advancedSnapshots.front().translucency == 25
                    && advancedSnapshots.front().revision
                        != initialSnapshots.front().revision,
                "StateAIAdvance advances loop frame and 25% translucency"
            );
        }
        if (tick == 8) {
            const std::vector<RuntimeParticleRenderSnapshot> fadedSnapshots =
                virusState.renderSnapshots(rules);
            ok &= expect(
                virusState.particles().size() == 3U
                    && virusState.particles().front().stateAI == 2
                    && fadedSnapshots.size() == 3U
                    && fadedSnapshots.front().frameIndex == 2
                    && fadedSnapshots.front().translucency == 50,
                "NumLoopFrames and Translucent50State render semantics"
            );
        }
        if (tick == 16) {
            ok &= expect(
                virusState.particles().size() == 3U
                    && virusState.particles().front().typeId == "VirusCloudD1"
                    && virusState.particles().front().position.z == 153.0,
                "NextParticle chain and offset"
            );
        }
    }
    ok &= expect(
        virusDamageCount == 15U,
        "VIRUSD gas produces repeated periodic damage"
    );
    ok &= expect(
        virusState.particles().empty(),
        "VIRUSD gas and dissipation stages expire"
    );

    RuntimeParticleSystemState gasState;
    const WarheadType* gasSpawner = rules.findWarheadType("GasSpawner");
    ok &= expect(gasSpawner != nullptr, "GasSpawner fixture warhead");
    if (gasSpawner != nullptr) {
        const RuntimeParticleSpawnResult gasSpawn = gasState.spawnFromWarhead(
            rules,
            *gasSpawner,
            context
        );
        ok &= expect(
            gasSpawn.warnings.empty()
                && gasSpawn.systemInstanceIds.size() == 1U
                && gasState.systems().size() == 1U
                && gasState.particles().size() == 1U,
            "stock Warhead Particle spawns a particle system"
        );
        std::size_t gasDamageCount = 0;
        for (int tick = 0; tick < 8; ++tick) {
            gasDamageCount += gasState.tick(rules).size();
        }
        ok &= expect(gasDamageCount >= 2U, "GasCloud periodic damage");
    }

    RuntimeParticleSystemState smokeState;
    if (smokeSpawner != nullptr) {
        const RuntimeParticleSpawnResult smokeSpawn = smokeState.spawnFromWarhead(
            rules,
            *smokeSpawner,
            context
        );
        ok &= expect(
            smokeSpawn.warnings.empty()
                && smokeSpawn.systemInstanceIds.size() == 1U,
            "explicit ParticleSystem spawns"
        );
        std::size_t smokeDamageCount = 0;
        for (int tick = 1; tick <= 16; ++tick) {
            smokeDamageCount += smokeState.tick(rules).size();
            if (tick == 2) {
                ok &= expect(
                    !smokeState.particles().empty()
                        && smokeState.particles().front().stateAI == 1,
                    "Smoke state advances"
                );
            }
        }
        ok &= expect(
            smokeDamageCount == 0U,
            "Smoke BehavesLike never emits damage events"
        );
        ok &= expect(
            smokeState.systems().empty() && smokeState.particles().empty(),
            "finite smoke system and particles disappear"
        );
    }

    RuntimeParticleSystemState deterministicA;
    RuntimeParticleSystemState deterministicB;
    deterministicA.spawnFromAnimation(rules, *virusDeath, context);
    deterministicB.spawnFromAnimation(rules, *virusDeath, context);
    for (int tick = 0; tick < 5; ++tick) {
        const std::vector<RuntimeParticleDamageEvent> eventsA =
            deterministicA.tick(rules);
        const std::vector<RuntimeParticleDamageEvent> eventsB =
            deterministicB.tick(rules);
        ok &= expect(sameEvents(eventsA, eventsB), "deterministic damage events");
        ok &= expect(
            deterministicA.stateHash() == deterministicB.stateHash(),
            "deterministic state hash"
        );
    }

    RuntimeParticleSystemState alignedEmptyState;
    alignedEmptyState.reset();
    ok &= expect(
        alignedEmptyState.alignEmptyStateTick(120)
            && alignedEmptyState.currentTick() == 120,
        "empty legacy state aligns to the host tick"
    );
    std::ostringstream alignedText;
    alignedEmptyState.save(alignedText);
    IniFile alignedIni;
    ok &= expect(
        alignedIni.loadFromString(alignedText.str()),
        "aligned empty particle INI parses"
    );
    RuntimeParticleSystemState alignedRestored;
    std::string alignedLoadError;
    ok &= expect(
        alignedRestored.load(alignedIni, rules, &alignedLoadError)
            && alignedRestored.currentTick() == 120
            && alignedRestored.stateHash() == alignedEmptyState.stateHash(),
        "aligned empty particle state survives save/load: "
            + alignedLoadError
    );
    const std::vector<RuntimeParticleDamageEvent> alignedEvents =
        alignedEmptyState.tick(rules);
    const std::vector<RuntimeParticleDamageEvent> alignedRestoredEvents =
        alignedRestored.tick(rules);
    ok &= expect(
        alignedEvents.empty()
            && alignedRestoredEvents.empty()
            && alignedEmptyState.currentTick() == 121
            && alignedRestored.currentTick() == 121
            && alignedRestored.stateHash() == alignedEmptyState.stateHash(),
        "aligned empty state advances on the next host tick"
    );
    const std::uint64_t hashBeforeRejectedAlignment =
        alignedEmptyState.stateHash();
    ok &= expect(
        !alignedEmptyState.alignEmptyStateTick(120)
            && !alignedEmptyState.alignEmptyStateTick(-1)
            && !alignedEmptyState.alignEmptyStateTick(
                std::numeric_limits<int>::max()
            )
            && alignedEmptyState.currentTick() == 121
            && alignedEmptyState.stateHash() == hashBeforeRejectedAlignment,
        "rewind and overflow alignment reject atomically"
    );

    RuntimeParticleSystemState nonEmptyParticleState;
    ok &= expect(
        nonEmptyParticleState.spawnParticle(
            rules,
            "VirusCloud1",
            context
        ) != 0,
        "non-empty alignment particle fixture spawns"
    );
    const std::uint64_t nonEmptyParticleHash =
        nonEmptyParticleState.stateHash();
    ok &= expect(
        !nonEmptyParticleState.alignEmptyStateTick(120)
            && nonEmptyParticleState.stateHash() == nonEmptyParticleHash,
        "particle-bearing state rejects alignment atomically"
    );

    RuntimeParticleSystemState nonEmptySystemState;
    ok &= expect(
        nonEmptySystemState.spawnSystem(
            rules,
            "GasCloudSys",
            context
        ) != 0,
        "non-empty alignment system fixture spawns"
    );
    const std::uint64_t nonEmptySystemHash = nonEmptySystemState.stateHash();
    ok &= expect(
        !nonEmptySystemState.alignEmptyStateTick(120)
            && nonEmptySystemState.stateHash() == nonEmptySystemHash,
        "system-bearing state rejects alignment atomically"
    );

    RuntimeParticleSystemState saveSource;
    saveSource.spawnFromAnimation(rules, *virusDeath, context);
    saveSource.tick(rules);
    saveSource.tick(rules);
    saveSource.tick(rules);
    const std::uint64_t hashBeforeSave = saveSource.stateHash();
    std::ostringstream savedText;
    saveSource.save(savedText);
    IniFile savedIni;
    ok &= expect(
        savedIni.loadFromString(savedText.str()),
        "saved particle INI parses"
    );
    RuntimeParticleSystemState restored;
    std::string loadError;
    RuntimeParticleReconcileReport loadReport;
    ok &= expect(
        restored.load(savedIni, rules, &loadError, &loadReport),
        "particle state reload: " + loadError
    );
    ok &= expect(
        loadReport.removedParticles == 0U
            && loadReport.removedSystems == 0U
            && restored.fixedTickHz() == 30
            && restored.stateHash() == hashBeforeSave,
        "save/load preserves complete state and hash"
    );
    for (int tick = 0; tick < 8; ++tick) {
        const std::vector<RuntimeParticleDamageEvent> originalEvents =
            saveSource.tick(rules);
        const std::vector<RuntimeParticleDamageEvent> restoredEvents =
            restored.tick(rules);
        ok &= expect(
            sameEvents(originalEvents, restoredEvents)
                && saveSource.stateHash() == restored.stateHash(),
            "save/load future evolution stays deterministic"
        );
    }

    IniFile emptyIni;
    emptyIni.loadFromString(
        "[InfantryTypes]\n[VehicleTypes]\n[AircraftTypes]\n"
        "[BuildingTypes]\n[OverlayTypes]\n[Tiberiums]\n"
        "[TerrainTypes]\n[Warheads]\n"
    );
    RulesDatabase emptyRules;
    emptyRules.loadFromIni(emptyIni);
    RuntimeParticleSystemState reconcileState;
    reconcileState.spawnFromAnimation(rules, *virusDeath, context);
    const RuntimeParticleReconcileReport reconcileReport =
        reconcileState.reconcile(emptyRules);
    ok &= expect(
        reconcileReport.removedParticles == 3U
            && reconcileState.particles().empty(),
        "reconcile removes instances whose authored types disappeared"
    );
    ok &= expect(
        reconcileState.spawnParticle(
            rules,
            "VirusCloud1",
            context
        ) == 4U,
        "reconcile preserves monotonic instance ids"
    );

    RuntimeParticleSystemState timingState;
    ok &= expect(
        timingState.fixedTickHz() == 30,
        "particle runtime defaults to the project 30 Hz tick rate"
    );
    const std::uint64_t timingId = timingState.spawnParticle(
        rules,
        "TimingParticle",
        context
    );
    std::size_t timingDamage = 0;
    for (int tick = 1; tick < 30; ++tick) {
        timingDamage += timingState.tick(rules).size();
    }
    ok &= expect(
        timingId != 0
            && timingDamage == 0U
            && timingState.particles().size() == 1U
            && timingState.particles().front().stateAI == 0,
        "15 authored frames remain active for 29 ticks at 30 Hz"
    );
    timingDamage += timingState.tick(rules).size();
    ok &= expect(
        timingDamage == 1U && timingState.particles().empty(),
        "15 authored frames equal 30 runtime ticks at 30 Hz"
    );

    RuntimeParticleSystemState timing15(15);
    timing15.spawnParticle(rules, "TimingParticle", context);
    std::size_t timing15Damage = 0;
    for (int tick = 0; tick < 15; ++tick) {
        timing15Damage += timing15.tick(rules).size();
    }
    ok &= expect(
        timing15Damage == 1U && timing15.particles().empty(),
        "15 Hz and 30 Hz timers expire at the same authored time"
    );

    RuntimeParticleSpawnContext lifetimeOverrideContext = context;
    lifetimeOverrideContext.systemLifetimeOverrideFrames = 2;
    RuntimeParticleSystemState lifetimeOverrideState(30);
    const std::uint64_t lifetimeOverrideId =
        lifetimeOverrideState.spawnSystem(
            rules,
            "GasCloudSys",
            lifetimeOverrideContext
        );
    ok &= expect(
        lifetimeOverrideId != 0
            && lifetimeOverrideState.systems().size() == 1U
            && lifetimeOverrideState.systems().front().remainingLifetime == 4,
        "per-spawn system lifetime override converts authored 15 Hz frames"
    );

    RuntimeParticleSystemState motion15(15);
    RuntimeParticleSystemState motion30(30);
    motion15.spawnSystem(rules, "MotionSys", context);
    motion30.spawnSystem(rules, "MotionSys", context);
    for (int tick = 0; tick < 3; ++tick) {
        ok &= expect(
            motion15.tick(rules).empty(),
            "15 Hz authored motion is non-damaging"
        );
    }
    for (int tick = 0; tick < 6; ++tick) {
        ok &= expect(
            motion30.tick(rules).empty(),
            "30 Hz authored motion is non-damaging"
        );
    }
    ok &= expect(
        motion15.particles().size() == 1U
            && motion30.particles().size() == 1U
            && samePoint(
                motion15.particles().front().position,
                motion30.particles().front().position
            )
            && samePoint(
                motion15.particles().front().velocity,
                motion30.particles().front().velocity
            )
            && motion15.particles().front().speed
                == motion30.particles().front().speed
            && motion15.particles().front().speed == 2.5
            && motion15.particles().front().stateAI
                == motion30.particles().front().stateAI
            && motion30.particles().front().remainingLifetime
                == motion15.particles().front().remainingLifetime * 2,
        "15 Hz N ticks and 30 Hz 2N ticks preserve particle-authored "
        "Velocity, Deacc, state, position and lifetime"
    );

    const ParticleType* windGasType =
        rules.findParticleType("WindGasParticle");
    const ParticleType* windSmokeType =
        rules.findParticleType("WindSmokeParticle");
    ok &= expect(
        windGasType != nullptr
            && windGasType->windEffect == 2
            && windSmokeType != nullptr
            && windSmokeType->windEffect == 5,
        "WindEffect fields"
    );
    RuntimeParticleSystemState wind15(15);
    RuntimeParticleSystemState wind30(30);
    const bool windSpawned =
        wind15.spawnParticle(rules, "WindGasParticle", context) != 0
        && wind15.spawnParticle(rules, "WindSmokeParticle", context) != 0
        && wind30.spawnParticle(rules, "WindGasParticle", context) != 0
        && wind30.spawnParticle(rules, "WindSmokeParticle", context) != 0;
    for (int tick = 0; tick < 4; ++tick) {
        wind15.tick(rules);
    }
    for (int tick = 0; tick < 8; ++tick) {
        wind30.tick(rules);
    }
    ok &= expect(
        windSpawned
            && wind15.particles().size() == 2U
            && wind30.particles().size() == 2U
            && samePoint(
                wind15.particles()[0].position,
                wind30.particles()[0].position
            )
            && samePoint(
                wind15.particles()[1].position,
                wind30.particles()[1].position
            )
            && wind15.particles()[0].position.x == context.position.x + 16.0
            && wind15.particles()[0].position.y == context.position.y + 16.0
            && wind15.particles()[1].position.x == context.position.x + 2.0
            && wind15.particles()[1].position.y == context.position.y + 4.0,
        "WindDirection/WindEffect preserve Gas and Smoke motion at 15/30 Hz"
    );

    RuntimeParticleSystemState color15(15);
    RuntimeParticleSystemState color30(30);
    const bool colorSpawned =
        color15.spawnParticle(rules, "ColorParticle", context) != 0
        && color30.spawnParticle(rules, "ColorParticle", context) != 0;
    const std::vector<RuntimeParticleRenderSnapshot> colorInitial =
        color15.renderSnapshots(rules);
    color15.tick(rules);
    color30.tick(rules);
    color30.tick(rules);
    const std::vector<RuntimeParticleRenderSnapshot> colorFrame15 =
        color15.renderSnapshots(rules);
    const std::vector<RuntimeParticleRenderSnapshot> colorFrame30 =
        color30.renderSnapshots(rules);
    ok &= expect(
        colorSpawned
            && colorInitial.size() == 1U
            && colorInitial.front().red == 0
            && colorInitial.front().green == 0
            && colorInitial.front().blue == 0
            && colorFrame15.size() == 1U
            && colorFrame30.size() == 1U
            && colorFrame15.front().red == 50
            && colorFrame15.front().green == 25
            && colorFrame15.front().blue == 0
            && colorFrame30.front().red == colorFrame15.front().red
            && colorFrame30.front().green == colorFrame15.front().green
            && colorFrame30.front().blue == colorFrame15.front().blue,
        "ColorSpeed interpolates ColorList at authored time at 15/30 Hz"
    );

    RuntimeParticleSpawnContext railgunContext = context;
    railgunContext.position = {256.0, 512.0, 128.0};
    railgunContext.direction = {32.0, 0.0, 0.0};
    RuntimeParticleSystemState railgunState(15);
    const std::uint64_t railgunId = railgunState.spawnSystem(
        rules,
        "RailgunSys",
        railgunContext
    );
    bool railgunLineAndSpiral = railgunState.particles().size() == 8U;
    double previousProjection = -1.0;
    for (const RuntimeParticleInstance& particle : railgunState.particles()) {
        const double projection = particle.position.x
            - railgunContext.position.x;
        const double radialDistance = std::sqrt(
            std::pow(particle.position.y - railgunContext.position.y, 2.0)
                + std::pow(
                    particle.position.z - railgunContext.position.z,
                    2.0
                )
        );
        railgunLineAndSpiral = railgunLineAndSpiral
            && projection > previousProjection
            && projection > 0.0
            && projection <= railgunContext.direction.x
            && near(radialDistance, 4.0, 1.0e-8);
        previousProjection = projection;
    }
    railgunLineAndSpiral = railgunLineAndSpiral
        && !near(
            railgunState.particles().front().position.y,
            railgunState.particles().back().position.y
        )
        && !near(
            railgunState.particles().front().position.z,
            railgunState.particles().back().position.z
        );
    ok &= expect(
        railgunId != 0 && railgunLineAndSpiral,
        "Railgun distributes capped particles along the authored spiral"
    );

    const std::uint64_t railgunHashBeforeSnapshots = railgunState.stateHash();
    const std::vector<RuntimeParticleBeamSnapshot> railgunBeams =
        railgunState.beamSnapshots(rules);
    const std::vector<RuntimeLocalLightSnapshot> railgunLights =
        railgunState.localLightSnapshots(rules);
    ok &= expect(
        railgunBeams.size() == 1U
            && railgunBeams.front().systemInstanceId == railgunId
            && railgunBeams.front().owner == context.owner
            && railgunBeams.front().fromWorldX == 1.0
            && railgunBeams.front().fromWorldY == 2.0
            && railgunBeams.front().fromWorldLevel == 0.5
            && railgunBeams.front().toWorldX == 288.0 / 256.0
            && railgunBeams.front().toWorldY == 2.0
            && railgunBeams.front().toWorldLevel == 0.5
            && railgunBeams.front().red == 10
            && railgunBeams.front().green == 20
            && railgunBeams.front().blue == 30
            && railgunBeams.front().thickness >= 1
            && railgunBeams.front().revision != 0,
        "Railgun beam snapshot preserves endpoints and LaserColor"
    );
    ok &= expect(
        railgunLights.size() == 1U
            && railgunLights.front().stableId == railgunId
            && railgunLights.front().owner == context.owner
            && railgunLights.front().worldX == 1.0
            && railgunLights.front().worldY == 2.0
            && railgunLights.front().worldLevel == 0.5
            && railgunLights.front().radiusPixels == 24
            && railgunLights.front().red == 10
            && railgunLights.front().green == 20
            && railgunLights.front().blue == 30
            && railgunLights.front().intensity > 0
            && railgunLights.front().revision != 0
            && railgunState.stateHash() == railgunHashBeforeSnapshots,
        "localLightSnapshots emits one derived system light, not one per particle"
    );
    railgunState.tick(rules);
    const std::vector<RuntimeLocalLightSnapshot> advancedRailgunLights =
        railgunState.localLightSnapshots(rules);
    ok &= expect(
        advancedRailgunLights.size() == 1U
            && advancedRailgunLights.front().stableId == railgunId
            && advancedRailgunLights.front().revision
                == railgunLights.front().revision,
        "persistent particle-system light survives simulation advance"
    );
    for (int tick = 1; tick < 10; ++tick) {
        railgunState.tick(rules);
    }
    ok &= expect(
        !railgunState.systems().empty()
            && railgunState.beamSnapshots(rules).empty()
            && railgunState.localLightSnapshots(rules).size() == 1U,
        "Railgun beam expires after its authored window while local light lives"
    );

    RuntimeParticleSystemState pulseLightState(15);
    RuntimeParticleSystemState pulseLightState30(30);
    const std::uint64_t pulseLightId = pulseLightState.spawnSystem(
        rules,
        "SparkSys",
        context
    );
    const std::uint64_t pulseLightId30 = pulseLightState30.spawnSystem(
        rules,
        "SparkSys",
        context
    );
    const bool pulseStartsDark = pulseLightState.localLightSnapshots(
        rules
    ).empty() && pulseLightState30.localLightSnapshots(rules).empty();
    pulseLightState.tick(rules);
    pulseLightState30.tick(rules);
    const std::vector<RuntimeLocalLightSnapshot> pulseLights =
        pulseLightState.localLightSnapshots(rules);
    const std::vector<RuntimeLocalLightSnapshot> pulseLights30 =
        pulseLightState30.localLightSnapshots(rules);
    const bool pulseStopped = pulseLightState.stopSystem(pulseLightId);
    const bool pulseStopped30 = pulseLightState30.stopSystem(pulseLightId30);
    pulseLightState.tick(rules);
    pulseLightState30.tick(rules);
    const bool pulseStillVisibleAtHalfRate =
        pulseLightState30.localLightSnapshots(rules).size() == 1U;
    pulseLightState30.tick(rules);
    ok &= expect(
        pulseLightId != 0
            && pulseLightId30 != 0
            && pulseStartsDark
            && pulseLights.size() == 1U
            && pulseLights30.size() == 1U
            && pulseLights.front().stableId == pulseLightId
            && pulseLights.front().radiusPixels == 15
            && pulseLights.front().red == 25
            && pulseLights.front().green == 20
            && pulseLights.front().blue == 255
            && pulseLights.front().intensity > 0
            && pulseStopped
            && pulseStopped30
            && pulseLightState.localLightSnapshots(rules).empty()
            && pulseStillVisibleAtHalfRate
            && pulseLightState30.localLightSnapshots(rules).empty(),
        "OneFrameLight lasts one authored frame at 15/30 Hz after emission"
    );

    RuntimeParticleSystemState cutoffState(15);
    const std::uint64_t cutoffId = cutoffState.spawnSystem(
        rules,
        "CutoffSmokeSys",
        context
    );
    for (int tick = 0; tick < 8; ++tick) {
        cutoffState.tick(rules);
    }
    const std::vector<int> expectedCutoffTranslucency{0, 0, 13, 25, 38};
    const std::vector<RuntimeParticleRenderSnapshot> cutoffSnapshots =
        cutoffState.renderSnapshots(rules);
    bool cutoffTranslucencyMatches = cutoffState.particles().size()
            == expectedCutoffTranslucency.size()
        && cutoffSnapshots.size() == expectedCutoffTranslucency.size();
    for (std::size_t index = 0;
        cutoffTranslucencyMatches
            && index < expectedCutoffTranslucency.size();
        ++index) {
        cutoffTranslucencyMatches = cutoffState.particles()[index].translucency
                == expectedCutoffTranslucency[index]
            && cutoffSnapshots[index].translucency
                == expectedCutoffTranslucency[index];
    }
    ok &= expect(
        cutoffId != 0
            && cutoffState.systems().size() == 1U
            && cutoffState.systems().front().spawnSerial == 5U
            && cutoffState.systems().front().spawnCountdown == 4
            && cutoffTranslucencyMatches,
        "SpawnCutoff slows smoke cadence and SpawnTranslucencyCutoff fades it"
    );

    RuntimeParticleSystemState sparkMotion15(15);
    RuntimeParticleSystemState sparkMotion30(30);
    sparkMotion15.spawnParticle(rules, "SparkParticle", context);
    sparkMotion30.spawnParticle(rules, "SparkParticle", context);
    const std::vector<RuntimeParticleRenderSnapshot> sparkSnapshots =
        sparkMotion15.renderSnapshots(rules);
    ok &= expect(
        sparkSnapshots.size() == 1U
            && sparkSnapshots.front().imageId.empty()
            && sparkSnapshots.front().renderKind
                == RuntimeParticleRenderKind::Spark
            && sparkSnapshots.front().red == 255
            && sparkSnapshots.front().green == 255
            && sparkSnapshots.front().blue == 100,
        "image-less spark snapshot did not carry deterministic render color"
    );
    for (int tick = 0; tick < 2; ++tick) {
        sparkMotion15.tick(rules);
    }
    for (int tick = 0; tick < 4; ++tick) {
        sparkMotion30.tick(rules);
    }
    ok &= expect(
        sparkMotion15.particles().size() == 1U
            && sparkMotion30.particles().size() == 1U
            && samePoint(
                sparkMotion15.particles().front().position,
                sparkMotion30.particles().front().position
            )
            && samePoint(
                sparkMotion15.particles().front().velocity,
                sparkMotion30.particles().front().velocity
            ),
        "X/Y/Z velocity has the same trajectory at 15 Hz and 30 Hz"
    );

    RuntimeParticleSystemState sparkState;
    RuntimeParticleSystemState emptyStoppedState;
    const std::uint64_t emptyStoppedId = emptyStoppedState.spawnSystem(
        rules,
        "SparkSys",
        context
    );
    ok &= expect(
        emptyStoppedId != 0
            && emptyStoppedState.particles().empty()
            && emptyStoppedState.stopSystem(emptyStoppedId)
            && emptyStoppedState.systems().empty(),
        "stopping an empty particle system immediately prunes it"
    );
    const std::uint64_t sourceStoppedId = emptyStoppedState.spawnSystem(
        rules,
        "SparkSys",
        context
    );
    ok &= expect(
        sourceStoppedId != 0
            && emptyStoppedState.stopSystemsForSource(
                context.sourceObjectId
            ) == 1U
            && emptyStoppedState.systems().empty(),
        "stopping an empty particle source immediately canonicalizes it"
    );
    const std::uint64_t sparkId = sparkState.spawnSystem(
        rules,
        "SparkSys",
        context
    );
    sparkState.tick(rules);
    sparkState.tick(rules);
    sparkState.tick(rules);
    ok &= expect(
        sparkId != 0
            && sparkState.particles().size() == 2U
            && sparkState.particles()[0].velocity.z > 0.0,
        "Spark behavior uses authored cadence and deterministic velocity"
    );
    ok &= expect(
        sparkState.stopSystem(sparkId),
        "particle system stop contract"
    );

    ArtType hostileAnimation = *virusDeath;
    hostileAnimation.id = "HostileParticleBurst";
    hostileAnimation.numParticles = std::numeric_limits<int>::max();
    RuntimeParticleSystemState particleCapState;
    RuntimeParticleSpawnResult cappedAnimation =
        particleCapState.spawnFromAnimation(
            rules,
            hostileAnimation,
            context
        );
    ok &= expect(
        cappedAnimation.particleInstanceIds.size()
                == runtimeParticleMaximumParticlesPerSpawn
            && particleCapState.particles().size()
                == runtimeParticleMaximumParticlesPerSpawn
            && cappedAnimation.warnings.size() == 1U,
        "hostile Animation.NumParticles was not deterministically truncated"
    );
    while (particleCapState.particles().size()
        < runtimeParticleMaximumParticles) {
        cappedAnimation = particleCapState.spawnFromAnimation(
            rules,
            hostileAnimation,
            context
        );
        if (cappedAnimation.particleInstanceIds.empty()) {
            break;
        }
    }
    std::string capError;
    const std::size_t particleCountAtCap =
        particleCapState.particles().size();
    ok &= expect(
        particleCountAtCap == runtimeParticleMaximumParticles
            && particleCapState.spawnParticle(
                rules,
                "VirusCloud1",
                context,
                &capError
            ) == 0
            && !capError.empty()
            && particleCapState.particles().size() == particleCountAtCap,
        "global runtime particle cap did not reject without mutation"
    );

    RuntimeParticleSystemState systemCapState;
    std::uint64_t lastSystemId = 0;
    for (std::size_t index = 0;
        index < runtimeParticleMaximumSystems;
        ++index) {
        lastSystemId = systemCapState.spawnSystem(
            rules,
            "GasCloudSys",
            context,
            &capError
        );
        if (lastSystemId == 0) {
            break;
        }
    }
    capError.clear();
    ok &= expect(
        lastSystemId != 0
            && systemCapState.systems().size()
                == runtimeParticleMaximumSystems
            && systemCapState.spawnSystem(
                rules,
                "GasCloudSys",
                context,
                &capError
            ) == 0
            && !capError.empty()
            && systemCapState.systems().size()
                == runtimeParticleMaximumSystems,
        "global runtime particle-system cap did not reject without mutation"
    );

    std::string oversizedSave = savedText.str();
    ok &= expect(
        replaceOnce(
            oversizedSave,
            "ParticleCount=3",
            "ParticleCount="
                + std::to_string(runtimeParticleMaximumParticles + 1U)
        ),
        "particle cap smoke could not patch saved count"
    );
    IniFile oversizedIni;
    ok &= expect(
        oversizedIni.loadFromString(oversizedSave),
        "oversized particle save did not parse"
    );
    const std::uint64_t hashBeforeRejectedLoad = restored.stateHash();
    loadError.clear();
    ok &= expect(
        !restored.load(oversizedIni, rules, &loadError, nullptr)
            && !loadError.empty()
            && restored.stateHash() == hashBeforeRejectedLoad,
        "oversized particle save was accepted or mutated live state"
    );

    if (!ok) {
        return 6;
    }
    std::cout << "particle smoke: PASS"
              << " systems=" << rules.getParticleSystemTypes().size()
              << " particles=" << rules.getParticleTypes().size()
              << " virusDamage=" << virusDamageCount
              << " hash=" << hashBeforeSave << "\n";
    return 0;
}
