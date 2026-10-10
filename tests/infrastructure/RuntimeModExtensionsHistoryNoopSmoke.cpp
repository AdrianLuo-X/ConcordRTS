#include "simulation/mod_extensions/RuntimeModExtensions.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

// Compile the actual production stage against a small host. The proof must be
// independent of all values returned by the hidden/temporal/power queries.
struct RuntimePerfTraceScope {
    RuntimePerfTraceScope(const char*, int) noexcept {}
};
struct TestSpecialMechanisms {
    std::unordered_set<int> parasites, dockedChildren, temporal;
    mutable std::uint64_t queries = 0;
    bool isParasiteSourceLimboed(int id) const { ++queries; return parasites.contains(id); }
    bool isCarrierChildDocked(int id) const { ++queries; return dockedChildren.contains(id); }
    bool isTemporalFrozen(int id) const { ++queries; return temporal.contains(id); }
};
struct RuntimeWindowState {
    GameWorld world;
    RulesDatabase rules;
    struct { int fixedTickHz = 30; } config;
    TestSpecialMechanisms specialMechanisms;
    mutable std::uint64_t powerQueries = 0;
    int tick = 0;
};
bool runtimeObjectHasOperationalPower(const RuntimeWindowState& state,
    const WorldObjectInstance& object) {
    ++state.powerQueries;
    return object.poweredOperational;
}
#include "simulation/mod_extensions/RuntimeModExtensionsRuntime.inl"

namespace {
int checks = 0;
void require(bool value, const char* reason) {
    ++checks;
    if (!value) throw std::runtime_error(reason);
}

const std::string longPlain = std::string(512, 'x') + "-plain";
const std::string longShield = std::string(512, 'x') + "-shield";
const std::string unicodePlain = "\xE5\x8D\x95\xE4\xBD\x8D-plain";
const std::string unicodeShield = "\xE5\x8D\x95\xE4\xBD\x8D-shield";

std::string fixture(bool plainHasShield = false) {
    return "[VehicleTypes]\n0=PLAIN\n1=SHIELD\n2=ZEROSHIELD\n3=INNATE\n4=ENTRY\n5=MISSING\n6="
        + longPlain + "\n7=" + longShield + "\n8=" + unicodePlain + "\n9=" + unicodeShield
        + "\n[ShieldTypes]\n0=BASIC\n1=ZERO\n[Warheads]\n0=KEEP\n"
          "[BASIC]\nStrength=100\nInitialStrength=50\nPowered=yes\nSelfHealing=10%\nSelfHealing.Rate=0.1\n"
          "[ZERO]\nStrength=0\n[PLAIN]\nStrength=100\n"
        + (plainHasShield ? "Shield.Type=BASIC\n" : "")
        + "[SHIELD]\nStrength=100\nShield.Type=BASIC\n[ZEROSHIELD]\nStrength=100\nShield.Type=ZERO\n"
          "[INNATE]\nStrength=100\nAttachEffect.Duration=15\nAttachEffect.InitialDelay=0\n"
          "[ENTRY]\nStrength=100\nAttachEffect.Duration=15\nAttachEffect.DiscardOnEntry=yes\nAttachEffect.Delay=15\n"
          "[MISSING]\nStrength=100\nShield.Type=UNREGISTERED\n[KEEP]\nAttachEffect.Duration=30\n"
        + "[" + longPlain + "]\nStrength=100\n[" + longShield + "]\nStrength=100\nShield.Type=BASIC\n"
        + "[" + unicodePlain + "]\nStrength=100\n[" + unicodeShield + "]\nStrength=100\nShield.Type=BASIC\n";
}
void load(RulesDatabase& rules, bool plainHasShield = false) {
    IniFile ini;
    require(ini.loadFromString(fixture(plainHasShield)), "fixture INI parsing failed");
    require(rules.loadFromIni(ini), "fixture rule loading failed");
}
WorldObjectInstance tombstone(int id, std::string type = "PLAIN") {
    WorldObjectInstance object;
    object.instanceId = id;
    object.kind = WorldObjectKind::Unit;
    object.type = std::move(type);
    object.owner = "Alpha";
    object.x = 2; object.y = 3; object.cell = 1538;
    object.alive = false; object.health = 0; object.mission = "Destroyed";
    object.modExtensions.shieldOnline = false;
    return object;
}

std::uint64_t modCodecHash(const GameWorld& world) {
    std::uint64_t result = 1469598103934665603ULL;
    for (const auto& object : world.getObjects()) {
        const auto text = runtimeSerializeModExtensions(object.modExtensions);
        for (unsigned char byte : text) { result ^= byte; result *= 1099511628211ULL; }
        result ^= 0; result *= 1099511628211ULL;
    }
    return result;
}
void compare(RuntimeWindowState& optimized, RuntimeWindowState& reference) {
    ++optimized.tick; ++reference.tick;
    bool optimizedChanged = false, referenceChanged = false;
    {
        RuntimeHistoryQueryReferenceScope route(false);
        optimizedChanged = advanceRuntimeModExtensions(optimized);
    }
    {
        RuntimeHistoryQueryReferenceScope route(true);
        referenceChanged = advanceRuntimeModExtensions(reference);
    }
    require(optimizedChanged == referenceChanged, "stage changed flag differs from original route");
    require(optimized.world.deterministicSimulationStateHash()
        == reference.world.deterministicSimulationStateHash(), "complete world hash differs");
    require(modCodecHash(optimized.world) == modCodecHash(reference.world), "all-object mod codec hash differs");
    const auto& a = optimized.world.getObjects(); const auto& b = reference.world.getObjects();
    require(a.size() == b.size(), "world population changed");
    // The world hash omits dormant, uninitialized mod fields for compatibility.
    // Compare their complete serialization too, including shieldOnline.
    for (std::size_t index = 0; index < a.size(); ++index)
        require(runtimeSerializeModExtensions(a[index].modExtensions)
            == runtimeSerializeModExtensions(b[index].modExtensions), "serialized dormant or active mod state differs");
}
}

int main() {
    try {
        RulesDatabase rules;
        load(rules);
        const auto safe = tombstone(1);
        {
            RuntimeInactiveModExtensionNoopStage stage(rules, false);
            require(stage.canSkip(safe), "plain tombstone was not proved inert");
            require(stage.canSkip(safe) && stage.profileLookups() == 1, "same complete type repeated rule lookups");
            require(stage.canSkip(tombstone(2, "plain")), "case variant was not resolved by original rules");
            require(stage.cachedTypes() == 2, "raw complete type cache keys were folded together");
            require(stage.canSkip(tombstone(3, "")), "empty missing profile was not proved inert");
            require(stage.canSkip(tombstone(4, unicodePlain)), "non-ASCII plain type was not proved inert");
            require(!stage.canSkip(tombstone(5, unicodeShield)), "non-ASCII shield type incorrectly skipped");
            require(stage.canSkip(tombstone(6, longPlain)), "long plain type incorrectly rejected");
            require(!stage.canSkip(tombstone(7, longShield)), "long type suffix collision hid authored shield");
            require(!stage.canSkip(tombstone(8, "ZEROSHIELD")), "zero-strength registered shield initialization skipped");
            require(!stage.canSkip(tombstone(9, "INNATE")), "innate initialization skipped");
            require(stage.canSkip(tombstone(10, "MISSING")), "missing registered shield did not follow null lookup semantics");
            auto object = safe; object.alive = true; object.health = 0;
            require(!stage.canSkip(object), "alive health-zero object skipped");
            object = safe; object.modExtensions.initialized = true;
            require(!stage.canSkip(object), "initialized dead object skipped");
            object = safe; object.modExtensions.shieldOnline = true;
            require(!stage.canSkip(object), "default online-shield transition skipped");
            object = safe; object.modExtensions.effects.push_back({"KEEP", true, 30, 0});
            require(!stage.canSkip(object), "death/entry effect cleanup skipped");
            for (int index = 0; index < 600; ++index)
                require(stage.canSkip(tombstone(100 + index, "missing-type-" + std::to_string(index))), "cache overflow changed proof");
            require(stage.cachedTypes() == RuntimeInactiveModExtensionNoopStage::MaximumCachedTypes,
                "stage type cache exceeded its fixed entry limit");
        }
        {
            RuntimeHistoryQueryReferenceScope outer(true);
            RuntimeInactiveModExtensionNoopStage stage(rules);
            require(!stage.canSkip(safe) && stage.profileLookups() == 0, "shared reference did not bypass proof");
            {
                RuntimeHistoryQueryReferenceScope inner(false);
                RuntimeInactiveModExtensionNoopStage nested(rules);
                require(nested.canSkip(safe), "nested shared optimized scope failed");
            }
            require(runtimeHistoryQueryReference(), "shared reference scope was not restored");
        }

        RuntimeWindowState optimized;
        optimized.rules = rules;
        for (int id = 1; id <= 22; ++id) optimized.world.accessObjects().push_back(tombstone(id));
        auto& objects = optimized.world.accessObjects();
        objects[1].modExtensions.shieldOnline = true; // Default state needs one real transition.
        objects[2].modExtensions.initialized = true; objects[2].modExtensions.technoType = "old-type";
        objects[3].alive = true; objects[3].health = 0;
        objects[4].mission = "InsideTransport"; objects[4].health = 100;
        objects[5].garrisonHostObjectId = 4;
        optimized.specialMechanisms.parasites.insert(7);
        optimized.specialMechanisms.dockedChildren.insert(8);
        optimized.specialMechanisms.temporal.insert(9);
        objects[9].modExtensions.effects.push_back({"KEEP", true, 30, 0});
        objects[10].mission = "InTransport";
        objects[10].modExtensions.initialized = true;
        objects[10].modExtensions.technoType = "plain";
        objects[10].modExtensions.effects.push_back({"ENTRY", false, 30, 1});
        objects[11].type = "SHIELD"; objects[11].poweredOperational = false;
        objects[12].type = "ZEROSHIELD";
        objects[13].type = "INNATE";
        objects[14].modExtensions.technoType = "dormant-type";
        objects[14].modExtensions.shieldType = "dormant-shield";
        objects[14].modExtensions.shieldCapacity = 200; objects[14].modExtensions.shieldHealth = 100;
        objects[14].modExtensions.shieldTimer = 15; objects[14].modExtensions.innateDelayTicks = 20;
        objects[15].type.clear(); objects[16].type = unicodePlain;
        objects[17].type = unicodeShield; objects[18].type = longPlain; objects[19].type = longShield;
        objects[20].alive = true; objects[20].health = 100; objects[20].type = "SHIELD";
        objects[20].poweredOperational = true;
        objects[21].type = "ENTRY"; objects[21].mission = "InsideTransport";
        for (int id = 100; id < 10100; ++id) {
            auto history = tombstone(id);
            history.modExtensions.shieldOnline = true;
            objects.push_back(std::move(history));
        }
        RuntimeWindowState reference = optimized;
        compare(optimized, reference); // All default online tombstones transition on the first tick.
        optimized.specialMechanisms.queries = reference.specialMechanisms.queries = 0;
        optimized.powerQueries = reference.powerQueries = 0;
        compare(optimized, reference);
        require(optimized.powerQueries < 22 && reference.powerQueries == reference.world.getObjects().size(),
            "inert history still performed power queries or reference stopped doing them");
        require(optimized.specialMechanisms.queries < 100 && reference.specialMechanisms.queries > 20000,
            "hidden/temporal queries still scale with inert history");

        // Preserve the complete fixture through type changes, resurrection,
        // transport exit, disappearing special states and same-address reload.
        for (int step = 0; step < 12; ++step) {
            for (auto* state : {&optimized, &reference}) {
                auto& a = state->world.accessObjects();
                if (step == 0) a[0].type = "SHIELD";
                if (step == 1) a[0].type = "PLAIN";
                if (step == 2) { a[0].alive = true; a[0].health = 100; }
                if (step == 3) { a[0].alive = false; a[0].health = 0; }
                if (step == 4) { a[4].mission = "Guard"; a[4].alive = true; a[4].health = 100; }
                if (step == 5) { state->specialMechanisms.parasites.clear(); state->specialMechanisms.temporal.clear(); }
                if (step == 6) load(state->rules, true);
                if (step == 8) state->rules = rules;
                if (step == 9) a[10].mission = "Destroyed";
                if (step == 10) a[20].poweredOperational = false;
                if (step == 11) a[20].poweredOperational = true;
            }
            compare(optimized, reference);
        }
        std::cout << "Mod history no-op proof passed: checks=" << checks
            << " worldObjects=" << optimized.world.getObjects().size()
            << " fullWorldHash=" << optimized.world.deterministicSimulationStateHash()
            << " fullModCodecHash=" << modCodecHash(optimized.world)
            << " completeTypeCacheLimit=" << RuntimeInactiveModExtensionNoopStage::MaximumCachedTypes
            << " historyTicks=14 transport/garrison/parasite/carrier/temporal/shield/innate/type/reload/resurrection=yes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Mod history no-op proof failed: " << error.what() << '\n';
        return 1;
    }
}
