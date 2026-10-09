#include "IniFile.h"
#include "RulesDatabase.h"
#include "RuntimeGarrisonSystem.h"

#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "garrison smoke: " << message << "\n";
        return false;
    }
    return true;
}

WorldObjectInstance makeHost(
    int id = 100,
    const std::string& type = "HOST",
    const std::string& owner = "Allies"
) {
    WorldObjectInstance host;
    host.instanceId = id;
    host.kind = WorldObjectKind::Structure;
    host.owner = owner;
    host.type = type;
    host.health = 1000;
    host.x = 20;
    host.y = 30;
    host.cell = 400;
    host.level = 2;
    host.onBridge = true;
    host.alive = true;
    return host;
}

WorldObjectInstance makeInfantry(
    int id,
    const std::string& type = "RIFLE",
    const std::string& owner = "Allies"
) {
    WorldObjectInstance infantry;
    infantry.instanceId = id;
    infantry.kind = WorldObjectKind::Infantry;
    infantry.owner = owner;
    infantry.type = type;
    infantry.health = 73;
    infantry.x = id;
    infantry.y = id + 1;
    infantry.cell = id + 10;
    infantry.level = 0;
    infantry.alive = true;
    infantry.veterancyExperience = 37;
    return infantry;
}

WorldObjectInstance* objectById(
    std::vector<WorldObjectInstance>& objects,
    int id
) {
    for (WorldObjectInstance& object : objects) {
        if (object.instanceId == id) {
            return &object;
        }
    }
    return nullptr;
}

const WorldObjectInstance* objectById(
    const std::vector<WorldObjectInstance>& objects,
    int id
) {
    for (const WorldObjectInstance& object : objects) {
        if (object.instanceId == id) {
            return &object;
        }
    }
    return nullptr;
}

std::vector<WorldObjectInstance> entryWorld(
    const std::string& occupantType = "RIFLE",
    const std::string& hostType = "HOST"
) {
    return {makeHost(100, hostType), makeInfantry(1, occupantType)};
}

RuntimeGarrisonExitPlacement exitAt(int offset) {
    RuntimeGarrisonExitPlacement placement;
    placement.x = 40 + offset;
    placement.y = 50 + offset;
    placement.cell = 600 + offset;
    placement.level = 1;
    return placement;
}

std::string evacuationFingerprint(
    const RuntimeGarrisonSystemState& system,
    const std::vector<WorldObjectInstance>& objects
) {
    std::ostringstream fingerprint;
    system.save(objects, fingerprint);
    for (const WorldObjectInstance& object : objects) {
        fingerprint << object.instanceId << '|'
                    << object.alive << '|'
                    << object.health << '|'
                    << object.mission << '|'
                    << object.x << '|'
                    << object.y << '|'
                    << object.cell << '|'
                    << object.level << '|'
                    << object.onBridge << '|'
                    << object.hasRenderPosition << '|'
                    << object.renderMoveActive << '|'
                    << object.movementAnimationDistance << '|'
                    << object.garrisonHostObjectId << '|'
                    << object.garrisonFireCursor << '|';
        for (const int occupantId : object.garrisonOccupantIds) {
            fingerprint << occupantId << ',';
        }
        fingerprint << '\n';
    }
    return fingerprint.str();
}

bool hasRecoveryKind(
    const RuntimeGarrisonReconcileReport& report,
    RuntimeGarrisonRecoveryKind kind
) {
    for (const RuntimeGarrisonRecoveryAction& action : report.actions) {
        if (action.kind == kind) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: concord_garrison_system_smoke <rules.ini>\n";
        return 2;
    }
    IniFile ini;
    RulesDatabase rules;
    if (!ini.loadFromFile(argv[1]) || !rules.loadFromIni(ini)) {
        std::cerr << "garrison smoke: fixture load failed\n";
        return 3;
    }

    bool ok = true;
    RuntimeGarrisonSystemState system;
    auto expectEntry = [&rules, &ok](
        RuntimeGarrisonEntryStatus expected,
        const std::function<void(std::vector<WorldObjectInstance>&)>& mutate,
        const RuntimeGarrisonEntryVeto& veto = RuntimeGarrisonEntryVeto()
    ) {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = entryWorld();
        mutate(objects);
        const RuntimeGarrisonEntryResult result = local.tryEnter(
            rules,
            objects,
            1,
            100,
            veto
        );
        ok &= expect(result.status == expected, "entry filter status");
    };

    {
        std::vector<WorldObjectInstance> objects = entryWorld();
        ok &= expect(
            system.tryEnter(rules, objects, 999, 100).status
                == RuntimeGarrisonEntryStatus::MissingOccupant,
            "missing occupant filter"
        );
        ok &= expect(
            system.tryEnter(rules, objects, 1, 999).status
                == RuntimeGarrisonEntryStatus::MissingHost,
            "missing host filter"
        );
        ok &= expect(
            system.tryEnter(rules, objects, 1, 1).status
                == RuntimeGarrisonEntryStatus::SameObject,
            "same object filter"
        );
    }
    expectEntry(RuntimeGarrisonEntryStatus::OccupantNotAlive, [](auto& objects) {
        objectById(objects, 1)->alive = false;
    });
    expectEntry(RuntimeGarrisonEntryStatus::OccupantNotInfantry, [](auto& objects) {
        objectById(objects, 1)->kind = WorldObjectKind::Unit;
    });
    expectEntry(RuntimeGarrisonEntryStatus::MissingOccupantType, [](auto& objects) {
        objectById(objects, 1)->type = "MISSING_INFANTRY";
    });
    expectEntry(RuntimeGarrisonEntryStatus::OccupantNotOccupier, [](auto& objects) {
        objectById(objects, 1)->type = "NONOCC";
    });
    expectEntry(RuntimeGarrisonEntryStatus::HostNotAlive, [](auto& objects) {
        objectById(objects, 100)->alive = false;
    });
    expectEntry(RuntimeGarrisonEntryStatus::HostNotStructure, [](auto& objects) {
        objectById(objects, 100)->kind = WorldObjectKind::Unit;
    });
    expectEntry(RuntimeGarrisonEntryStatus::MissingHostType, [](auto& objects) {
        objectById(objects, 100)->type = "MISSING_HOST";
    });
    expectEntry(RuntimeGarrisonEntryStatus::HostNotOccupiable, [](auto& objects) {
        objectById(objects, 100)->type = "NOTOCC";
    });
    expectEntry(RuntimeGarrisonEntryStatus::HostHasNoCapacity, [](auto& objects) {
        objectById(objects, 100)->type = "NOCAP";
    });
    expectEntry(RuntimeGarrisonEntryStatus::DifferentOwner, [](auto& objects) {
        objectById(objects, 1)->owner = "Soviets";
    });
    expectEntry(RuntimeGarrisonEntryStatus::HostAtConditionRed, [](auto& objects) {
        objectById(objects, 100)->health = 250;
    });
    expectEntry(RuntimeGarrisonEntryStatus::OccupantInTransport, [](auto& objects) {
        WorldObjectInstance transport;
        transport.instanceId = 200;
        transport.kind = WorldObjectKind::Unit;
        transport.transportPassengerIds = {1};
        objects.push_back(transport);
    });
    expectEntry(
        RuntimeGarrisonEntryStatus::OccupantAlreadyGarrisoned,
        [](auto& objects) {
            objectById(objects, 1)->garrisonHostObjectId = 101;
        }
    );
    expectEntry(
        RuntimeGarrisonEntryStatus::Vetoed,
        [](auto&) {},
        [](const auto&, const auto&) { return std::string("mind link"); }
    );

    // Entry is value-preserving, capacity-limited, and ordered by entry.
    {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = {
            makeHost(), makeInfantry(2), makeInfantry(1), makeInfantry(3)
        };
        WorldObjectInstance* first = objectById(objects, 2);
        first->veterancy = 1;
        const int savedHealth = first->health;
        const int savedExperience = first->veterancyExperience;
        ok &= expect(local.tryEnter(rules, objects, 2, 100).entered(), "first entry");
        ok &= expect(local.tryEnter(rules, objects, 1, 100).entered(), "second entry");
        ok &= expect(
            local.tryEnter(rules, objects, 3, 100).status
                == RuntimeGarrisonEntryStatus::HostAtCapacity,
            "capacity filter"
        );
        const WorldObjectInstance* host = objectById(objects, 100);
        first = objectById(objects, 2);
        ok &= expect(
            host->garrisonOccupantIds == std::vector<int>({2, 1}),
            "manifest preserves deterministic entry order"
        );
        ok &= expect(
            !first->alive
                && first->garrisonHostObjectId == 100
                && first->health == savedHealth
                && first->veterancy == 1
                && first->veterancyExperience == savedExperience
                && first->x == host->x
                && first->y == host->y
                && first->cell == host->cell
                && first->level == host->level,
            "entry preserves progression and synchronizes host position"
        );
        objectById(objects, 100)->x = 77;
        objectById(objects, 100)->cell = 707;
        local.syncHostOccupants(objects, 100);
        ok &= expect(
            objectById(objects, 2)->x == 77
                && objectById(objects, 2)->cell == 707,
            "host movement synchronization"
        );
    }

    // Actual occupant weapon selection, fixed range, multipliers, cooldowns,
    // and round-robin cursor are all deterministic.
    {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = {
            makeHost(100, "HOST4"),
            makeInfantry(1, "RIFLE"),
            makeInfantry(2, "RIFLE")
        };
        objectById(objects, 2)->veterancy = 2;
        local.tryEnter(rules, objects, 1, 100);
        local.tryEnter(rules, objects, 2, 100);
        const auto rookie = local.takeFirePlan(rules, objects, 100, 900, 0);
        const auto blocked = local.takeFirePlan(rules, objects, 100, 900, 0);
        const auto elite = local.takeFirePlan(rules, objects, 100, 900, 13);
        ok &= expect(
            rookie.has_value()
                && rookie->occupantObjectId == 1
                && rookie->weaponId == "OccupyGun"
                && rookie->baseDamage == 100
                && rookie->damage == 120
                && rookie->cooldownTicks == 13
                && rookie->range == 5.0
                && rookie->minimumRange == 0.5
                && rookie->burst == 2,
            "rookie occupy weapon plan and stock multipliers"
        );
        ok &= expect(
            elite.has_value()
                && elite->occupantObjectId == 2
                && elite->weaponId == "EliteOccupyGun"
                && elite->damage == 240
                && elite->cooldownTicks == 25,
            "elite occupy weapon plan"
        );
        ok &= expect(
            !blocked.has_value()
                && !local.takeFirePlan(rules, objects, 100, 900, 13),
            "host-wide cooldown blocks same-tick and early reuse"
        );
        const auto next = local.takeFirePlan(rules, objects, 100, 900, 38);
        ok &= expect(
            next.has_value() && next->occupantObjectId == 1,
            "round-robin resumes at first ready occupant"
        );
    }
    {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = {
            makeHost(100, "HOST4"),
            makeInfantry(1, "FALLBACK"),
            makeInfantry(2, "FALLBACK"),
            makeInfantry(3, "PRIMARYONLY")
        };
        objectById(objects, 1)->veterancy = 2;
        objectById(objects, 2)->veterancy = 1;
        objectById(objects, 3)->veterancy = 2;
        local.tryEnter(rules, objects, 1, 100);
        local.tryEnter(rules, objects, 2, 100);
        local.tryEnter(rules, objects, 3, 100);
        const auto eliteFallback = local.takeFirePlan(rules, objects, 100, -1, 0);
        const auto veteranFallback = local.takeFirePlan(rules, objects, 100, -1, 6);
        const auto primaryFallback = local.takeFirePlan(rules, objects, 100, -1, 13);
        ok &= expect(
            eliteFallback && eliteFallback->weaponId == "ElitePrimaryGun",
            "elite fallback reaches ElitePrimary"
        );
        ok &= expect(
            veteranFallback && veteranFallback->weaponId == "PrimaryGun",
            "veteran fallback does not use ElitePrimary"
        );
        ok &= expect(
            primaryFallback && primaryFallback->weaponId == "PrimaryGun",
            "elite fallback reaches Primary"
        );
    }
    {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = {
            makeHost(100, "NOFIRE"), makeInfantry(1)
        };
        local.tryEnter(rules, objects, 1, 100);
        ok &= expect(
            !local.takeFirePlan(rules, objects, 100, 5, 0),
            "CanOccupyFire=no blocks a plan"
        );
    }

    // Manual exits are transactional; forced evacuation is deterministic and
    // makes blocked-destruction policy explicit.
    {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = entryWorld();
        local.tryEnter(rules, objects, 1, 100);
        ok &= expect(
            local.exitOccupant(objects, 1, std::nullopt).status
                == RuntimeGarrisonExitStatus::NoExitPlacement
                && objectById(objects, 1)->garrisonHostObjectId == 100,
            "failed manual exit keeps relation intact"
        );
        const int health = objectById(objects, 1)->health;
        const int experience = objectById(objects, 1)->veterancyExperience;
        ok &= expect(
            local.exitOccupant(objects, 1, exitAt(1)).exited(),
            "manual exit succeeds with placement"
        );
        ok &= expect(
            objectById(objects, 1)->alive
                && objectById(objects, 1)->garrisonHostObjectId < 0
                && objectById(objects, 1)->health == health
                && objectById(objects, 1)->veterancyExperience == experience
                && objectById(objects, 1)->cell == 601
                && objectById(objects, 100)->garrisonOccupantIds.empty(),
            "manual exit preserves occupant progression"
        );
    }
    {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = {
            makeHost(), makeInfantry(1), makeInfantry(2)
        };
        local.tryEnter(rules, objects, 1, 100);
        local.tryEnter(rules, objects, 2, 100);
        const auto actions = local.forceEvacuate(
            objects,
            100,
            RuntimeGarrisonEvacuationReason::Forced,
            [](const auto& occupant, const auto&, auto, std::size_t) {
                return occupant.instanceId == 1
                    ? std::optional<RuntimeGarrisonExitPlacement>(exitAt(1))
                    : std::nullopt;
            },
            false
        );
        ok &= expect(
            actions.size() == 2U
                && actions[0].disposition
                    == RuntimeGarrisonEvacuationDisposition::Exited
                && actions[1].disposition
                    == RuntimeGarrisonEvacuationDisposition::KeptInside
                && objectById(objects, 2)->garrisonHostObjectId == 100,
            "forced evacuation can retain blocked occupants"
        );
        objectById(objects, 100)->health = 250;
        const auto redActions = local.evacuateIfConditionRed(
            rules,
            objects,
            100,
            {},
            true
        );
        ok &= expect(
            redActions.size() == 1U
                && redActions[0].disposition
                    == RuntimeGarrisonEvacuationDisposition::DestroyedNoExit
                && objectById(objects, 2)->health == 0
                && objectById(objects, 100)->garrisonOccupantIds.empty(),
            "ConditionRed evacuation destroys an occupant with no exit"
        );
    }
    {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = {
            makeHost(100, "HOST4"),
            makeInfantry(1),
            makeInfantry(2),
            makeInfantry(3)
        };
        local.tryEnter(rules, objects, 1, 100);
        local.tryEnter(rules, objects, 2, 100);
        local.tryEnter(rules, objects, 3, 100);
        const std::optional<RuntimeGarrisonFirePlan> initialPlan =
            local.takeFirePlan(rules, objects, 100, 900, 7);
        const int retainedCooldown = local.nextEligibleFireTick(100);
        std::vector<int> resolverOrder;
        std::vector<std::size_t> resolverIndices;
        const RuntimeGarrisonSelectiveEvacuationResult result =
            local.forceEvacuateOccupants(
                objects,
                100,
                {3, 2},
                RuntimeGarrisonEvacuationReason::Forced,
                [&resolverOrder, &resolverIndices](
                    const auto& occupant,
                    const auto&,
                    auto,
                    std::size_t manifestIndex
                ) {
                    resolverOrder.push_back(occupant.instanceId);
                    resolverIndices.push_back(manifestIndex);
                    return occupant.instanceId == 2
                        ? std::optional<RuntimeGarrisonExitPlacement>(exitAt(2))
                        : std::nullopt;
                },
                true
            );
        const WorldObjectInstance* host = objectById(objects, 100);
        ok &= expect(
            initialPlan.has_value()
                && result.applied()
                && result.actions.size() == 2U
                && result.actions[0].occupantId == 2
                && result.actions[0].disposition
                    == RuntimeGarrisonEvacuationDisposition::Exited
                && result.actions[1].occupantId == 3
                && result.actions[1].disposition
                    == RuntimeGarrisonEvacuationDisposition::DestroyedNoExit
                && resolverOrder == std::vector<int>({2, 3})
                && resolverIndices == std::vector<std::size_t>({1U, 2U}),
            "selective evacuation uses manifest order and blocked policy"
        );
        ok &= expect(
            host != nullptr
                && host->garrisonOccupantIds == std::vector<int>({1})
                && objectById(objects, 1)->garrisonHostObjectId == 100
                && objectById(objects, 2)->alive
                && objectById(objects, 2)->cell == 602
                && objectById(objects, 3)->health == 0
                && local.nextEligibleFireTick(100) == retainedCooldown,
            "selective evacuation retains other manifest and cooldown state"
        );
        const std::optional<RuntimeGarrisonFirePlan> retainedPlan =
            local.takeFirePlan(rules, objects, 100, 901, retainedCooldown);
        ok &= expect(
            retainedPlan.has_value()
                && retainedPlan->occupantObjectId == 1,
            "selective evacuation keeps the remaining firing authority"
        );
    }
    {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = {
            makeHost(), makeInfantry(1), makeInfantry(2)
        };
        local.tryEnter(rules, objects, 1, 100);
        const std::string before = evacuationFingerprint(local, objects);
        int resolverCalls = 0;
        const RuntimeGarrisonExitResolver resolver =
            [&resolverCalls](const auto&, const auto&, auto, std::size_t) {
                ++resolverCalls;
                return std::optional<RuntimeGarrisonExitPlacement>(exitAt(1));
            };
        const RuntimeGarrisonSelectiveEvacuationResult missingHost =
            local.forceEvacuateOccupants(
                objects,
                999,
                {1},
                RuntimeGarrisonEvacuationReason::Forced,
                resolver
            );
        const RuntimeGarrisonSelectiveEvacuationResult duplicate =
            local.forceEvacuateOccupants(
                objects,
                100,
                {1, 1},
                RuntimeGarrisonEvacuationReason::Forced,
                resolver
            );
        const RuntimeGarrisonSelectiveEvacuationResult notInHost =
            local.forceEvacuateOccupants(
                objects,
                100,
                {1, 2},
                RuntimeGarrisonEvacuationReason::Forced,
                resolver
            );
        const RuntimeGarrisonSelectiveEvacuationResult empty =
            local.forceEvacuateOccupants(
                objects,
                100,
                {},
                RuntimeGarrisonEvacuationReason::Forced,
                resolver
            );
        ok &= expect(
            missingHost.status
                    == RuntimeGarrisonSelectiveEvacuationStatus::InvalidHost
                && duplicate.status
                    == RuntimeGarrisonSelectiveEvacuationStatus::DuplicateOccupant
                && duplicate.rejectedOccupantId == 1
                && notInHost.status
                    == RuntimeGarrisonSelectiveEvacuationStatus::OccupantNotInHost
                && notInHost.rejectedOccupantId == 2
                && empty.applied()
                && empty.actions.empty()
                && resolverCalls == 0
                && evacuationFingerprint(local, objects) == before,
            "invalid selective batches reject atomically before resolution"
        );
    }
    {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = {
            makeHost(), makeInfantry(1), makeInfantry(2)
        };
        local.tryEnter(rules, objects, 1, 100);
        local.tryEnter(rules, objects, 2, 100);
        const auto actions = local.handleHostDestruction(objects, 100, true);
        ok &= expect(
            actions.size() == 2U
                && actions[0].disposition
                    == RuntimeGarrisonEvacuationDisposition::DestroyedWithHost
                && objectById(objects, 1)->health == 0
                && objectById(objects, 2)->health == 0
                && objectById(objects, 100)->garrisonOccupantIds.empty(),
            "immediate host destruction destroys all occupants"
        );
    }
    {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = {
            makeHost(), makeInfantry(1), makeInfantry(2)
        };
        local.tryEnter(rules, objects, 1, 100);
        local.tryEnter(rules, objects, 2, 100);
        const auto actions = local.handleHostDestruction(
            objects,
            100,
            false,
            [](const auto& occupant, const auto&, auto, std::size_t) {
                return occupant.instanceId == 1
                    ? std::optional<RuntimeGarrisonExitPlacement>(exitAt(1))
                    : std::nullopt;
            }
        );
        ok &= expect(
            actions.size() == 2U
                && objectById(objects, 1)->alive
                && objectById(objects, 2)->health == 0,
            "non-immediate host destruction evacuates or destroys blocked"
        );
    }

    // Save/load owns relation, cursor, cooldown and tuning state.  Invalid
    // syntax is transactional while valid-but-corrupt relations are filtered.
    {
        RuntimeGarrisonSystemState savedState;
        std::vector<WorldObjectInstance> savedObjects = {
            makeHost(), makeInfantry(1), makeInfantry(2)
        };
        savedState.tryEnter(rules, savedObjects, 1, 100);
        savedState.tryEnter(rules, savedObjects, 2, 100);
        savedState.takeFirePlan(rules, savedObjects, 100, 900, 7);
        std::ostringstream text;
        savedState.save(savedObjects, text);
        IniFile savedIni;
        ok &= expect(savedIni.loadFromString(text.str()), "saved INI parses");

        std::vector<WorldObjectInstance> loadedObjects = {
            makeHost(), makeInfantry(1), makeInfantry(2)
        };
        RuntimeGarrisonSystemState loadedState;
        RuntimeGarrisonReconcileReport report;
        std::string error;
        ok &= expect(
            loadedState.load(
                savedIni,
                rules,
                loadedObjects,
                &error,
                &report
            ),
            "garrison save loads"
        );
        ok &= expect(
            loadedState.stateHash(loadedObjects)
                == savedState.stateHash(savedObjects)
                && loadedState.nextEligibleFireTick(100)
                    == savedState.nextEligibleFireTick(100)
                && objectById(loadedObjects, 100)->garrisonOccupantIds
                    == std::vector<int>({1, 2})
                && !objectById(loadedObjects, 1)->alive,
            "save/load/hash round trip"
        );

        const std::uint64_t beforeHash = loadedState.stateHash(loadedObjects);
        const std::vector<int> beforeManifest =
            objectById(loadedObjects, 100)->garrisonOccupantIds;
        IniFile malformed;
        malformed.loadFromString(
            "[GarrisonRuntime]\nVersion=1\nFixedTickHz=nope\n"
        );
        ok &= expect(
            !loadedState.load(malformed, rules, loadedObjects, &error)
                && loadedState.stateHash(loadedObjects) == beforeHash
                && objectById(loadedObjects, 100)->garrisonOccupantIds
                    == beforeManifest,
            "malformed load is transactional"
        );
    }
    {
        const std::string corruptText =
            "[GarrisonRuntime]\n"
            "Version=1\nFixedTickHz=30\nConditionRed=.25\n"
            "DamageMultiplier=1.2\nROFMultiplier=1.2\nWeaponRange=5\n"
            "HostCount=2\nCooldownCount=2\n\n"
            "[GarrisonRuntime.Host.0]\nHostId=100\nCursor=99\n"
            "Occupants=1,1,999,2,3\n\n"
            "[GarrisonRuntime.Host.1]\nHostId=999\nCursor=0\nOccupants=4\n\n"
            "[GarrisonRuntime.Cooldown.0]\nHostId=100\nNextTick=50\n\n"
            "[GarrisonRuntime.Cooldown.1]\nHostId=999\nNextTick=50\n";
        IniFile corruptIni;
        corruptIni.loadFromString(corruptText);
        std::vector<WorldObjectInstance> objects = {
            makeHost(), makeInfantry(1), makeInfantry(2),
            makeInfantry(3), makeInfantry(4)
        };
        RuntimeGarrisonSystemState local;
        RuntimeGarrisonReconcileReport report;
        std::string error;
        ok &= expect(
            local.load(corruptIni, rules, objects, &error, &report),
            "syntactically valid corrupt save loads with filtering"
        );
        ok &= expect(
            objectById(objects, 100)->garrisonOccupantIds
                == std::vector<int>({1, 2})
                && objectById(objects, 1)->garrisonHostObjectId == 100
                && objectById(objects, 2)->garrisonHostObjectId == 100
                && objectById(objects, 3)->garrisonHostObjectId < 0
                && local.nextEligibleFireTick(999) == 0
                && report.removedRelations >= 4U
                && report.removedCooldowns >= 1U,
            "corrupt relation, capacity, duplicate, and cooldown filtering"
        );
    }

    // Reconcile recovers reverse-only and unambiguous legacy saves, but emits
    // an action instead of guessing when a legacy host is ambiguous.
    {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = entryWorld();
        WorldObjectInstance* occupant = objectById(objects, 1);
        occupant->alive = false;
        occupant->mission = "Garrisoned";
        occupant->garrisonHostObjectId = 100;
        const RuntimeGarrisonReconcileReport report = local.reconcile(
            rules,
            objects
        );
        ok &= expect(
            objectById(objects, 100)->garrisonOccupantIds
                == std::vector<int>({1})
                && hasRecoveryKind(
                    report,
                    RuntimeGarrisonRecoveryKind::RecoveredReverseOnly
                ),
            "reverse-only relation recovery"
        );
    }
    {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = entryWorld();
        WorldObjectInstance* occupant = objectById(objects, 1);
        const WorldObjectInstance* host = objectById(objects, 100);
        occupant->alive = false;
        occupant->mission = "Occupied";
        occupant->x = host->x;
        occupant->y = host->y;
        occupant->cell = host->cell;
        const RuntimeGarrisonReconcileReport report = local.reconcile(
            rules,
            objects
        );
        ok &= expect(
            occupant->garrisonHostObjectId == 100
                && hasRecoveryKind(
                    report,
                    RuntimeGarrisonRecoveryKind::RecoveredLegacyOccupant
                ),
            "unique colocated legacy relation recovery"
        );
    }
    {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = {
            makeHost(100), makeHost(101), makeInfantry(1)
        };
        WorldObjectInstance* occupant = objectById(objects, 1);
        occupant->alive = false;
        occupant->mission = "Occupied";
        occupant->x = 20;
        occupant->y = 30;
        occupant->cell = 400;
        const RuntimeGarrisonReconcileReport report = local.reconcile(
            rules,
            objects
        );
        ok &= expect(
            occupant->garrisonHostObjectId < 0
                && hasRecoveryKind(
                    report,
                    RuntimeGarrisonRecoveryKind::NeedsExitPlacement
                ),
            "ambiguous legacy occupant requires explicit recovery placement"
        );
    }
    {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = {
            makeHost(101), makeHost(100), makeInfantry(1)
        };
        objectById(objects, 101)->garrisonOccupantIds = {1};
        objectById(objects, 100)->garrisonOccupantIds = {1};
        objectById(objects, 1)->alive = false;
        objectById(objects, 1)->mission = "Garrisoned";
        const RuntimeGarrisonReconcileReport report = local.reconcile(
            rules,
            objects
        );
        ok &= expect(
            objectById(objects, 100)->garrisonOccupantIds
                == std::vector<int>({1})
                && objectById(objects, 101)->garrisonOccupantIds.empty()
                && objectById(objects, 1)->garrisonHostObjectId == 100
                && report.removedRelations >= 1U,
            "double-manifest corruption chooses the lowest host id"
        );
    }
    {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = {
            makeHost(), makeInfantry(1), makeInfantry(2)
        };
        WorldObjectInstance* first = objectById(objects, 1);
        WorldObjectInstance* second = objectById(objects, 2);
        first->alive = false;
        second->alive = false;
        first->mission = "Garrisoned";
        second->mission = "Garrisoned";
        first->garrisonHostObjectId = 2;
        second->garrisonHostObjectId = 1;
        const RuntimeGarrisonReconcileReport report = local.reconcile(
            rules,
            objects
        );
        ok &= expect(
            first->garrisonHostObjectId < 0
                && second->garrisonHostObjectId < 0
                && hasRecoveryKind(
                    report,
                    RuntimeGarrisonRecoveryKind::NeedsExitPlacement
                ),
            "circular non-host reverse links are cleared without recursion"
        );
    }
    {
        RuntimeGarrisonSystemState local;
        std::vector<WorldObjectInstance> objects = entryWorld();
        WorldObjectInstance* occupant = objectById(objects, 1);
        occupant->alive = false;
        occupant->mission = "Garrisoned";
        occupant->garrisonHostObjectId = 999;
        const RuntimeGarrisonReconcileReport report = local.reconcile(
            rules,
            objects
        );
        ok &= expect(
            occupant->garrisonHostObjectId < 0
                && hasRecoveryKind(
                    report,
                    RuntimeGarrisonRecoveryKind::NeedsExitPlacement
                ),
            "nonexistent reverse host is filtered with recovery action"
        );
    }

    return ok ? 0 : 1;
}
