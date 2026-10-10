#include "simulation/buildings/RuntimeRepairDepotSystem.h"

#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <iostream>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "repair depot smoke failure: " << message << '\n';
    }
    return condition;
}

std::string canonicalSmokeOwner(std::string owner) {
    std::transform(
        owner.begin(),
        owner.end(),
        owner.begin(),
        [](char character) {
            return character >= 'A' && character <= 'Z'
                ? static_cast<char>(character - 'A' + 'a')
                : character;
        }
    );
    if (owner == "player") {
        return "Player";
    }
    if (owner == "ally") {
        return "Ally";
    }
    if (owner == "enemy") {
        return "Enemy";
    }
    return owner;
}

WorldObjectInstance makeHost(
    int id,
    const std::string& owner,
    const std::string& type
) {
    WorldObjectInstance object;
    object.instanceId = id;
    object.kind = WorldObjectKind::Structure;
    object.owner = owner;
    object.type = type;
    object.health = 1000;
    object.alive = true;
    object.poweredOperational = true;
    object.x = id / 10;
    object.y = id / 10;
    return object;
}

WorldObjectInstance makeUnit(
    int id,
    const std::string& owner,
    const std::string& type,
    int health
) {
    WorldObjectInstance object;
    object.instanceId = id;
    object.kind = WorldObjectKind::Unit;
    object.owner = owner;
    object.type = type;
    object.health = health;
    object.alive = true;
    object.x = id;
    object.y = id;
    object.mission = "Guard";
    return object;
}

WorldObjectInstance* findMutable(
    std::vector<WorldObjectInstance>& objects,
    int objectId
) {
    for (WorldObjectInstance& object : objects) {
        if (object.instanceId == objectId) {
            return &object;
        }
    }
    return nullptr;
}

const RuntimeRepairDepotAction* findAction(
    const std::vector<RuntimeRepairDepotAction>& actions,
    RuntimeRepairDepotActionKind kind,
    int unitId = -1
) {
    for (const RuntimeRepairDepotAction& action : actions) {
        if (action.kind == kind
            && (unitId < 0 || action.unitId == unitId)) {
            return &action;
        }
    }
    return nullptr;
}

const RuntimeRepairDepotRelation* findRelation(
    const RuntimeRepairDepotSystemState& state,
    int unitId
) {
    const auto found = std::find_if(
        state.relations().begin(),
        state.relations().end(),
        [unitId](const RuntimeRepairDepotRelation& relation) {
            return relation.unitId == unitId;
        }
    );
    return found == state.relations().end() ? nullptr : &*found;
}

RuntimeRepairDepotCell moveTarget(
    const RuntimeRepairDepotCommandResult& result
) {
    if (result.actions.empty()
        || !result.actions.front().hasTargetCell) {
        return {};
    }
    return result.actions.front().targetCell;
}

int actionCount(
    const std::vector<RuntimeRepairDepotAction>& actions,
    RuntimeRepairDepotActionKind kind
) {
    return static_cast<int>(std::count_if(
        actions.begin(),
        actions.end(),
        [kind](const RuntimeRepairDepotAction& action) {
            return action.kind == kind;
        }
    ));
}

struct SmokeWorld {
    const RulesDatabase* rules = nullptr;
    std::vector<WorldObjectInstance> objects;
    std::map<std::string, int> credits;
    std::map<int, int> refunds;
    std::set<int> transportedUnits;
    std::set<int> blockedUnits;
    std::set<std::tuple<int, int, int>> blockedCellsByUnit;
    std::set<int> cancelledReservations;
    std::set<std::pair<int, int>> externallyOccupiedDocks;
    std::set<std::pair<int, int>> unavailableDocks;
    std::set<std::pair<int, int>> unavailableQueueSlots;
    bool queueEnabled = false;
    bool collapseQueueCells = false;

    RuntimeRepairDepotContext context() {
        RuntimeRepairDepotContext result;
        result.rules = rules;
        result.objects = &objects;
        result.areAllied = [](const std::string& left, const std::string& right) {
            const bool leftFriendly = left == "Player" || left == "Ally";
            const bool rightFriendly = right == "Player" || right == "Ally";
            return leftFriendly && rightFriendly;
        };
        result.creditBalance = [this](const std::string& owner) {
            const auto found = credits.find(owner);
            return found == credits.end() ? 0 : found->second;
        };
        result.resolveDockCell = [this](
            int hostId,
            int dockIndex,
            int unitId
        ) -> std::optional<RuntimeRepairDepotCell> {
            (void)unitId;
            if (unavailableDocks.contains({hostId, dockIndex})) {
                return std::nullopt;
            }
            return RuntimeRepairDepotCell{
                hostId / 10 + dockIndex,
                hostId / 20 + dockIndex,
                false
            };
        };
        if (queueEnabled) {
            result.resolveQueueCell = [this](
                int hostId,
                int queueIndex,
                int unitId
            ) -> std::optional<RuntimeRepairDepotCell> {
                (void)unitId;
                if (unavailableQueueSlots.find({hostId, queueIndex})
                    != unavailableQueueSlots.end()) {
                    return std::nullopt;
                }
                const int offset = collapseQueueCells ? 0 : queueIndex;
                return RuntimeRepairDepotCell{
                    hostId / 10 + 10 + offset,
                    hostId / 20 + 10 + offset,
                    false
                };
            };
        }
        result.resolveRallyCell = [](
            int hostId,
            int dockIndex,
            int unitId
        ) -> std::optional<RuntimeRepairDepotCell> {
            (void)unitId;
            return RuntimeRepairDepotCell{
                hostId / 10 + dockIndex + 4,
                hostId / 20 + dockIndex + 4,
                false
            };
        };
        result.canPathTo = [this](
            int unitId,
            const RuntimeRepairDepotCell& cell
        ) {
            return cell.x >= 0
                && cell.y >= 0
                && blockedUnits.find(unitId) == blockedUnits.end()
                && blockedCellsByUnit.find({unitId, cell.x, cell.y})
                    == blockedCellsByUnit.end();
        };
        result.isReservationActive = [this](
            int hostId,
            int dockIndex,
            int unitId
        ) {
            (void)hostId;
            (void)dockIndex;
            return cancelledReservations.find(unitId)
                == cancelledReservations.end();
        };
        result.isTransported = [this](int unitId) {
            return transportedUnits.find(unitId) != transportedUnits.end();
        };
        result.canonicalOwnerId = [](const std::string& owner) {
            return canonicalSmokeOwner(owner);
        };
        result.isDockOccupied = [this](int hostId, int dockIndex) {
            if (externallyOccupiedDocks.find({hostId, dockIndex})
                != externallyOccupiedDocks.end()) {
                return true;
            }
            return std::any_of(
                objects.begin(),
                objects.end(),
                [hostId, dockIndex](const WorldObjectInstance& object) {
                    return object.dockedToObjectId == hostId
                        && object.dockIndex == dockIndex;
                }
            );
        };
        result.resolveHostDomain = [this](int hostId)
            -> std::optional<RuntimeRepairDepotDomain> {
            const WorldObjectInstance* host = nullptr;
            for (const WorldObjectInstance& object : objects) {
                if (object.instanceId == hostId) {
                    host = &object;
                    break;
                }
            }
            if (host == nullptr || host->kind != WorldObjectKind::Structure) {
                return std::nullopt;
            }
            return host->type == "NAVYARD"
                ? RuntimeRepairDepotDomain::Naval
                : RuntimeRepairDepotDomain::Ground;
        };
        result.resolveUnitDomain = [this](int unitId)
            -> std::optional<RuntimeRepairDepotDomain> {
            const WorldObjectInstance* unit = nullptr;
            for (const WorldObjectInstance& object : objects) {
                if (object.instanceId == unitId) {
                    unit = &object;
                    break;
                }
            }
            if (unit == nullptr || unit->kind != WorldObjectKind::Unit) {
                return std::nullopt;
            }
            return unit->type == "BOAT"
                ? RuntimeRepairDepotDomain::Naval
                : RuntimeRepairDepotDomain::Ground;
        };
        result.refundValue = [this](int unitId) -> std::optional<int> {
            const auto found = refunds.find(unitId);
            return found == refunds.end()
                ? std::nullopt
                : std::optional<int>(found->second);
        };
        return result;
    }

    void apply(const std::vector<RuntimeRepairDepotAction>& actions) {
        for (const RuntimeRepairDepotAction& action : actions) {
            WorldObjectInstance* unit = findMutable(objects, action.unitId);
            if (unit == nullptr) {
                continue;
            }
            if (action.kind == RuntimeRepairDepotActionKind::MoveToQueue
                || action.kind == RuntimeRepairDepotActionKind::MoveToDock) {
                if (action.hasTargetCell) {
                    unit->x = action.targetCell.x;
                    unit->y = action.targetCell.y;
                    unit->onBridge = action.targetCell.onBridge;
                }
                unit->mission = action.kind
                        == RuntimeRepairDepotActionKind::MoveToQueue
                    ? "MoveToRepairQueue"
                    : "MoveToRepairDock";
            } else if (action.kind == RuntimeRepairDepotActionKind::Attach) {
                unit->dockedToObjectId = action.hostId;
                unit->dockIndex = action.dockIndex;
                if (action.hasTargetCell) {
                    unit->x = action.targetCell.x;
                    unit->y = action.targetCell.y;
                    unit->onBridge = action.targetCell.onBridge;
                }
                unit->mission = "RepairDock";
            } else if (action.kind == RuntimeRepairDepotActionKind::Repair) {
                unit->health = action.healthAfter;
                credits[action.owner] = std::max(
                    0,
                    credits[action.owner] - action.creditCost
                );
            } else if (action.kind
                    == RuntimeRepairDepotActionKind::DestroyDockedUnit) {
                unit->dockedToObjectId = -1;
                unit->dockIndex = -1;
                unit->health = 0;
                unit->alive = false;
            } else if (action.kind
                    == RuntimeRepairDepotActionKind::SellDockedUnit) {
                credits[canonicalSmokeOwner(action.expectedOwner)] +=
                    action.refundValue;
                unit->dockedToObjectId = -1;
                unit->dockIndex = -1;
                unit->health = 0;
                unit->alive = false;
            } else if (action.kind
                    == RuntimeRepairDepotActionKind::TransferDockedUnitOwner) {
                if (unit->owner == action.expectedOwner) {
                    unit->owner = action.newOwner;
                }
            } else if (action.kind
                    == RuntimeRepairDepotActionKind::UndockToRally
                || action.kind == RuntimeRepairDepotActionKind::Release) {
                if (unit->dockedToObjectId == action.hostId
                    && unit->dockIndex == action.dockIndex) {
                    unit->dockedToObjectId = -1;
                    unit->dockIndex = -1;
                }
                if (action.hasTargetCell) {
                    unit->x = action.targetCell.x;
                    unit->y = action.targetCell.y;
                    unit->onBridge = action.targetCell.onBridge;
                    unit->mission = "Move";
                } else {
                    unit->mission = "Guard";
                }
            }
        }
    }
};

SmokeWorld makeWorld(const RulesDatabase& rules) {
    SmokeWorld world;
    world.rules = &rules;
    world.credits = {{"Player", 1000}, {"Ally", 1000}, {"Enemy", 1000}};
    for (int unitId = 10; unitId <= 40; ++unitId) {
        world.refunds[unitId] = 500;
    }
    world.objects.push_back(makeHost(100, "Player", "DEPOT"));
    world.objects.push_back(makeHost(200, "Player", "RELOADDEPOT"));
    world.objects.push_back(makeHost(300, "Player", "NOTADEPOT"));
    world.objects.push_back(makeHost(400, "Player", "RELOADONLY"));
    world.objects.push_back(makeHost(500, "Player", "NAVYARD"));
    world.objects.push_back(makeUnit(10, "Player", "TANK", 40));
    world.objects.push_back(makeUnit(11, "Ally", "TANK", 50));
    world.objects.push_back(makeUnit(12, "Player", "TANK", 60));
    world.objects.push_back(makeUnit(13, "Player", "NOREPAIR", 40));
    world.objects.push_back(makeUnit(14, "Player", "AIRCAR", 40));
    world.objects.push_back(makeUnit(15, "Enemy", "TANK", 40));
    world.objects.push_back(makeUnit(16, "Player", "TANK", 100));
    world.objects.push_back(makeUnit(17, "Player", "BOAT", 40));
    world.objects.push_back(makeUnit(18, "Player", "TANK", 40));
    world.objects.push_back(makeUnit(19, "Player", "TANK", 40));
    world.objects.push_back(makeUnit(20, "Player", "TANK", 40));
    world.objects.push_back(makeUnit(21, "Player", "TANK", 96));
    world.objects.push_back(makeUnit(22, "Player", "BOAT", 40));
    world.objects.push_back(makeUnit(23, "Player", "BOAT", 96));
    world.objects.push_back(makeUnit(24, "Player", "BOAT", 40));
    world.objects.push_back(makeUnit(25, "Player", "BOAT", 40));
    return world;
}

bool reserveAndAttach(
    RuntimeRepairDepotSystemState& state,
    SmokeWorld& world,
    int hostId,
    int unitId,
    int tick,
    int* dockIndex = nullptr
) {
    RuntimeRepairDepotContext context = world.context();
    const RuntimeRepairDepotCommandResult reserve = state.reserveDock(
        context,
        hostId,
        unitId,
        tick
    );
    if (!reserve.accepted
        || reserve.actions.size() != 1
        || reserve.actions.front().kind
            != RuntimeRepairDepotActionKind::MoveToDock
        || !reserve.actions.front().hasTargetCell) {
        return false;
    }
    if (dockIndex != nullptr) {
        *dockIndex = reserve.dockIndex;
    }
    world.apply(reserve.actions);
    const RuntimeRepairDepotCommandResult attach = state.attachAtDock(
        context,
        hostId,
        unitId,
        reserve.actions.front().targetCell,
        tick
    );
    if (!attach.accepted
        || attach.actions.size() != 1
        || attach.actions.front().kind
            != RuntimeRepairDepotActionKind::Attach) {
        return false;
    }
    world.apply(attach.actions);
    return true;
}

bool expectVeto(
    RuntimeRepairDepotSystemState& state,
    SmokeWorld& world,
    int hostId,
    int unitId,
    RuntimeRepairDepotVeto expected
) {
    const RuntimeRepairDepotCommandResult result = state.reserveDock(
        world.context(),
        hostId,
        unitId,
        0
    );
    return expect(
        !result.accepted && result.veto == expected,
        std::string("expected veto ") + runtimeRepairDepotVetoName(expected)
    );
}

bool replaceFirst(
    std::string& text,
    const std::string& from,
    const std::string& to
) {
    const std::size_t position = text.find(from);
    if (position == std::string::npos) {
        return false;
    }
    text.replace(position, from.size(), to);
    return true;
}

bool runAnimationSmoke(const RulesDatabase& rules) {
    using Phase = RuntimeRepairDepotAnimationPhase;
    bool success = true;
    const auto phaseIs = [](const RuntimeRepairDepotSystemState& state,
                            Phase phase, int age) {
        return state.animations().size() == 1
            && state.animations().front().hostId == 100
            && state.animations().front().phase == phase
            && state.animations().front().ageTicks == age;
    };
    SmokeWorld world = makeWorld(rules);
    RuntimeRepairDepotSystemState state(10);
    success &= expect(reserveAndAttach(state, world, 100, 10, 0)
        && phaseIs(state, Phase::Extending, 0), "attach starts native arm extension");
    auto context = world.context();
    context.animationDurationTicks = [](int, Phase phase) {
        return phase == Phase::Extending ? 3 : 2;
    };
    const auto step = [&](int tick) { world.apply(state.tick(context, tick)); };
    step(0);
    success &= expect(phaseIs(state, Phase::Extending, 1)
        && findMutable(world.objects, 10)->health == 48
        && world.credits["Player"] == 988, "animation preserves first repair pulse");
    step(1);
    const auto roundTrip = [&]() {
        std::ostringstream saved; state.save(saved);
        IniFile ini; RuntimeRepairDepotSystemState restored;
        std::string error;
        return ini.loadFromString(saved.str())
            && restored.load(ini, context, &error)
            && restored.animations() == state.animations()
            && restored.stateHash() == state.stateHash();
    };
    success &= expect(phaseIs(state, Phase::Extending, 2) && roundTrip(),
        "extension save/load retains phase and age without advancing");
    const auto beforeReconcile = state.animations();
    (void)state.reconcile(context);
    (void)state.tick(context, 1);
    success &= expect(state.animations() == beforeReconcile,
        "reconcile and repeated tick cannot advance native animation");
    step(2);
    success &= expect(phaseIs(state, Phase::Repairing, 0) && roundTrip(),
        "extension boundary enters saved repair loop");
    findMutable(world.objects, 100)->poweredOperational = false;
    for (int tick = 3; tick <= 10; ++tick) step(tick);
    success &= expect(phaseIs(state, Phase::Repairing, 0)
        && world.credits["Player"] == 988 && roundTrip(),
        "power outage freezes arm phase and age and paid repair");
    findMutable(world.objects, 100)->poweredOperational = true;
    step(11);
    success &= expect(phaseIs(state, Phase::Repairing, 1),
        "power recovery resumes without consuming outage time");
    world.apply(state.releaseUnit(context, 10, "cancel"));
    success &= expect(phaseIs(state, Phase::Retracting, 0)
        && state.relations().empty() && roundTrip(),
        "cancel saves retraction after the final relation disappears");
    step(12);
    success &= expect(phaseIs(state, Phase::Retracting, 1),
        "retraction retains its final native frame");
    step(13);
    success &= expect(state.animations().empty(), "retraction returns to idle");

    for (const int credits : {0, 1, 5, 12}) {
        SmokeWorld poor = makeWorld(rules); poor.credits["Player"] = credits;
        RuntimeRepairDepotSystemState probe(10);
        success &= expect(reserveAndAttach(probe, poor, 100, 10, 0),
            "limited funds animation setup");
        poor.apply(probe.tick(poor.context(), 0));
        success &= expect(phaseIs(probe, Phase::Retracting, 0)
            && findMutable(poor.objects, 10)->health
                == (credits == 5 ? 43 : credits == 12 ? 48 : 40)
            && poor.credits["Player"] == (credits <= 1 ? credits : 0),
            "zero/unaffordable/last paid balance retracts without changing charge rounding");
        poor.credits["Player"] = 1000;
        poor.apply(probe.tick(poor.context(), 1));
        success &= expect(phaseIs(probe, Phase::Extending, 0),
            "funds restored restart extension");
    }
    {
        SmokeWorld complete = makeWorld(rules);
        RuntimeRepairDepotSystemState probe(10);
        success &= expect(reserveAndAttach(probe, complete, 100, 21, 0),
            "completion animation setup");
        complete.apply(probe.tick(complete.context(), 0));
        success &= expect(phaseIs(probe, Phase::Retracting, 0)
            && probe.relations().empty()
            && findMutable(complete.objects, 21)->health == 100,
            "full health starts retraction on the final paid pulse");
    }
    for (const auto kind : {RuntimeRepairDepotHostTransitionKind::Captured,
                           RuntimeRepairDepotHostTransitionKind::Destroyed,
                           RuntimeRepairDepotHostTransitionKind::Sold}) {
        SmokeWorld transitioned = makeWorld(rules);
        RuntimeRepairDepotSystemState probe(10);
        success &= expect(reserveAndAttach(probe, transitioned, 100, 10, 0),
            "host animation transition setup");
        transitioned.apply(probe.tick(transitioned.context(), 0));
        RuntimeRepairDepotHostTransitionRequest request;
        request.hostId = 100; request.expectedOwner = "Player";
        request.newOwner = "Ally"; request.kind = kind;
        const auto result = probe.transitionHost(transitioned.context(), request);
        success &= expect(result.accepted
            && (kind == RuntimeRepairDepotHostTransitionKind::Captured
                ? phaseIs(probe, Phase::Extending, 1) : probe.animations().empty()),
            "capture keeps native phase while destruction/sale removes it");
    }
    {
        SmokeWorld naval = makeWorld(rules);
        RuntimeRepairDepotSystemState probe(10);
        success &= expect(reserveAndAttach(probe, naval, 500, 22, 0),
            "naval repair animation setup");
        naval.apply(probe.tick(naval.context(), 0));
        success &= expect(probe.animations().size() == 1
            && probe.animations().front().hostId == 500
            && probe.animations().front().phase == Phase::Extending
            && probe.animations().front().ageTicks == 1
            && findMutable(naval.objects, 22)->health == 48
            && naval.credits["Player"] == 988,
            "naval host starts its own animation without changing the paid repair pulse");
        findMutable(naval.objects, 500)->poweredOperational = false;
        for (int tick = 1; tick <= 6; ++tick) {
            naval.apply(probe.tick(naval.context(), tick));
        }
        success &= expect(!findMutable(naval.objects, 500)->poweredOperational
            && probe.animations().size() == 1
            && probe.animations().front().hostId == 500
            && probe.animations().front().phase == Phase::Repairing
            && probe.animations().front().ageTicks == 2
            && findMutable(naval.objects, 22)->health == 56
            && naval.credits["Player"] == 976,
            "Powered=no naval repair honors its normal service and animation clocks on a deficient grid");
        std::ostringstream saved; probe.save(saved);
        IniFile ini; RuntimeRepairDepotSystemState loaded;
        success &= expect(ini.loadFromString(saved.str())
            && loaded.load(ini, naval.context())
            && loaded.animations() == probe.animations()
            && loaded.stateHash() == probe.stateHash(),
            "naval animation save/load retains its host, phase and age");
    }
    {
        SmokeWorld savedWorld = makeWorld(rules);
        RuntimeRepairDepotSystemState source(10);
        success &= expect(reserveAndAttach(source, savedWorld, 100, 10, 0),
            "animation save validation setup");
        std::ostringstream stream; source.save(stream);
        const auto originalHash = source.stateHash();
        for (const auto replacement : {std::pair{"AgeTicks=0", "AgeTicks=-1"},
                                      std::pair{"Phase=Extending", "Phase=Unknown"},
                                      std::pair{"AnimationCount=1", "AnimationCount=4097"}}) {
            std::string corrupt = stream.str();
            replaceFirst(corrupt, replacement.first, replacement.second);
            IniFile ini; ini.loadFromString(corrupt); std::string error;
            success &= expect(!source.load(ini, savedWorld.context(), &error)
                && !error.empty() && source.stateHash() == originalHash,
                "invalid animation saves fail atomically");
        }
        std::string old = stream.str();
        replaceFirst(old, "Version=4", "Version=3");
        old.erase(old.find("[RepairDepotRuntime.Animation.0]"));
        IniFile oldIni; oldIni.loadFromString(old);
        RuntimeRepairDepotSystemState legacy;
        success &= expect(legacy.load(oldIni, savedWorld.context())
            && legacy.animations().empty(), "schema 3 loads without invented elapsed animation");
        savedWorld.apply(legacy.tick(savedWorld.context(), 0));
        success &= expect(phaseIs(legacy, Phase::Extending, 0),
            "legacy active repair starts its arm on the next simulation tick");
    }
    {
        IniFile freeIni; RulesDatabase freeRules;
        freeIni.loadFromString("[General]\nRepairPercent=0\nRepairStep=8\nURepairRate=.01\n"
            "[VehicleTypes]\n0=TANK\n[BuildingTypes]\n0=DEPOT\n"
            "[TANK]\nStrength=100\nCost=1000\nRepairable=yes\n"
            "[DEPOT]\nStrength=1000\nUnitRepair=yes\nNumberOfDocks=2\n");
        success &= expect(freeRules.loadFromIni(freeIni), "free repair animation rules");
        SmokeWorld freeWorld = makeWorld(freeRules); freeWorld.credits["Player"] = 0;
        RuntimeRepairDepotSystemState probe(10);
        success &= expect(reserveAndAttach(probe, freeWorld, 100, 10, 0),
            "free repair animation setup");
        freeWorld.apply(probe.tick(freeWorld.context(), 0));
        success &= expect(phaseIs(probe, Phase::Extending, 1)
            && findMutable(freeWorld.objects, 10)->health == 48
            && freeWorld.credits["Player"] == 0,
            "free repair still animates with zero balance");
    }
    return success;
}

bool runBlockedCompletionSmoke() {
    IniFile ini;
    RulesDatabase rules;
    if (!expect(ini.loadFromString(
            "[General]\nRepairPercent=15%\nRepairStep=8\nURepairRate=.01\n"
            "[VehicleTypes]\n0=TANK\n1=BOAT\n"
            "[BuildingTypes]\n0=GADEPT\n1=NADEPT\n2=DEPOT\n3=NAVYARD\n"
            "[TANK]\nStrength=100\nCost=1000\nRepairable=yes\n"
            "[BOAT]\nStrength=100\nCost=1000\nRepairable=yes\nNaval=yes\nWaterBound=yes\n"
            "[GADEPT]\nStrength=1000\nUnitRepair=yes\nNumberOfDocks=1\n"
            "[NADEPT]\nStrength=1000\nUnitRepair=yes\nNumberOfDocks=1\n"
            "[DEPOT]\nStrength=1000\nUnitRepair=yes\nNumberOfDocks=1\n"
            "[NAVYARD]\nStrength=1000\nUnitRepair=yes\nNumberOfDocks=1\nNaval=yes\nWaterBound=yes\n")
            && rules.loadFromIni(ini), "blocked-completion local rules load")) {
        return false;
    }
    bool success = true;
    for (const std::string hostType : {"GADEPT", "NADEPT"}) {
        for (const int hz : {30, 60}) {
            const std::string label = hostType + '/' + std::to_string(hz);
            SmokeWorld world;
            world.rules = &rules;
            world.credits["Player"] = 1000;
            world.objects = {makeHost(100, "Player", hostType),
                makeUnit(21, "Player", "TANK", 96)};
            RuntimeRepairDepotSystemState state(hz);
            bool exitAvailable = false;
            const RuntimeRepairDepotCell exit{14, 9, false};
            const auto contextFor = [&](SmokeWorld& targetWorld) {
                auto context = targetWorld.context();
                context.resolveRallyCell = [&](int, int, int)
                    -> std::optional<RuntimeRepairDepotCell> {
                    return exitAvailable ? std::optional<RuntimeRepairDepotCell>(exit) : std::nullopt;
                };
                return context;
            };
            if (!expect(reserveAndAttach(state, world, 100, 21, 0),
                    label + " blocked exit setup attaches damaged unit")) {
                return false;
            }
            auto context = contextFor(world);
            const auto first = state.tick(context, 0);
            const auto* repair = findAction(first, RuntimeRepairDepotActionKind::Repair, 21);
            success &= expect(repair && repair->healthBefore == 96 && repair->healthAfter == 100
                    && repair->healedHitPoints == 4 && repair->creditCost == 12
                    && actionCount(first, RuntimeRepairDepotActionKind::Repair) == 1
                    && !findAction(first, RuntimeRepairDepotActionKind::UndockToRally)
                    && !findAction(first, RuntimeRepairDepotActionKind::Release),
                label + " final paid repair waits for a real exit");
            world.apply(first);
            const auto waiting = [&](const RuntimeRepairDepotSystemState& currentState, SmokeWorld& currentWorld) {
                const auto* relation = findRelation(currentState, 21);
                const auto* unit = findMutable(currentWorld.objects, 21);
                return relation && relation->phase == RuntimeRepairDepotRelationPhase::Docked
                    && relation->hasRepaired && unit && unit->health == 100
                    && unit->dockedToObjectId == 100 && unit->dockIndex == 0
                    && unit->x == relation->dockCell.x && unit->y == relation->dockCell.y
                    && currentWorld.credits["Player"] == 988;
            };
            success &= expect(waiting(state, world), label + " completed unit retains dock and paid health");
            const int interval = state.remainingServiceTicks(100);
            if (!expect(interval > 0, label + " blocked exit retains original service timer")) return false;
            const int waitEnd = interval * 3;
            for (int tick = 1; tick <= waitEnd; ++tick) {
                const auto reconciled = state.reconcile(context);
                success &= expect(reconciled.actions.empty() && reconciled.releasedRelations == 0,
                    label + " full-health reconcile cannot release a blocked dock");
                const auto actions = state.tick(context, tick);
                world.apply(actions);
                success &= expect(actions.empty() && waiting(state, world),
                    label + " repeated blocked service clocks cannot undock or debit again");
            }
            std::ostringstream saved;
            state.save(saved);
            IniFile savedIni;
            SmokeWorld restoredWorld = world;
            auto restoredContext = contextFor(restoredWorld);
            RuntimeRepairDepotSystemState restored;
            RuntimeRepairDepotReconcileReport loadReport;
            std::string loadError;
            if (!expect(savedIni.loadFromString(saved.str())
                    && restored.load(savedIni, restoredContext, &loadError, &loadReport)
                    && loadError.empty() && loadReport.releasedRelations == 0
                    && loadReport.actions.empty() && restored.stateHash() == state.stateHash()
                    && waiting(restored, restoredWorld),
                label + " waiting full-health dock round-trips existing save/hash")) return false;
            exitAvailable = true;
            const int releaseTick = waitEnd + state.remainingServiceTicks(100);
            for (int tick = waitEnd + 1; tick <= releaseTick; ++tick) {
                const auto reconciled = state.reconcile(context);
                const auto restoredReconciled = restored.reconcile(restoredContext);
                success &= expect(reconciled.actions.empty() && restoredReconciled.actions.empty(),
                    label + " an opened exit still observes the normal service cadence");
                const auto actions = state.tick(context, tick);
                const auto restoredActions = restored.tick(restoredContext, tick);
                world.apply(actions);
                restoredWorld.apply(restoredActions);
                success &= expect(!findAction(actions, RuntimeRepairDepotActionKind::Repair)
                        && !findAction(restoredActions, RuntimeRepairDepotActionKind::Repair)
                        && state.stateHash() == restored.stateHash()
                        && world.credits["Player"] == 988 && restoredWorld.credits["Player"] == 988,
                    label + " restored exit retry preserves clock and the one original charge");
                if (tick < releaseTick) {
                    success &= expect(actions.empty() && restoredActions.empty()
                            && waiting(state, world) && waiting(restored, restoredWorld),
                        label + " waits until the next repair clock before departure");
                } else {
                    const auto* departure = findAction(actions, RuntimeRepairDepotActionKind::UndockToRally, 21);
                    const auto* restoredDeparture = findAction(restoredActions, RuntimeRepairDepotActionKind::UndockToRally, 21);
                    const auto* unit = findMutable(world.objects, 21);
                    const auto* restoredUnit = findMutable(restoredWorld.objects, 21);
                    success &= expect(actions.size() == 1 && restoredActions.size() == 1
                            && departure && departure->hasTargetCell && departure->targetCell == exit
                            && restoredDeparture && restoredDeparture->hasTargetCell && restoredDeparture->targetCell == exit
                            && state.relations().empty() && restored.relations().empty()
                            && state.remainingServiceTicks(100) == -1 && restored.remainingServiceTicks(100) == -1
                            && unit && unit->health == 100 && unit->dockedToObjectId == -1
                            && unit->x == exit.x && unit->y == exit.y
                            && restoredUnit && restoredUnit->health == 100 && restoredUnit->dockedToObjectId == -1
                            && restoredUnit->x == exit.x && restoredUnit->y == exit.y,
                        label + " opened exit naturally releases once with its real rally target");
                }
            }
            success &= expect(state.tick(context, releaseTick + 1).empty()
                    && restored.tick(restoredContext, releaseTick + 1).empty()
                    && world.credits["Player"] == 988 && restoredWorld.credits["Player"] == 988,
                label + " completed departure cannot repeat a release or repair charge");
        }
    }
    // Hosts outside the stock ground-pad contract and contexts without a
    // rally resolver retain their previous completion behavior.
    for (const std::string hostType : {"DEPOT", "NAVYARD", "GADEPT"}) {
        SmokeWorld world;
        world.rules = &rules;
        world.credits["Player"] = 1000;
        world.objects = {makeHost(100, "Player", hostType),
            makeUnit(21, "Player", hostType == "NAVYARD" ? "BOAT" : "TANK", 96)};
        RuntimeRepairDepotSystemState state(30);
        if (!expect(reserveAndAttach(state, world, 100, 21, 0), "legacy completion scope setup")) return false;
        auto context = world.context();
        if (hostType == "GADEPT") context.resolveRallyCell = {};
        else context.resolveRallyCell = [](int, int, int) -> std::optional<RuntimeRepairDepotCell> { return std::nullopt; };
        const auto actions = state.tick(context, 0);
        const auto* departure = findAction(actions, RuntimeRepairDepotActionKind::UndockToRally, 21);
        world.apply(actions);
        success &= expect(actionCount(actions, RuntimeRepairDepotActionKind::Repair) == 1
                && departure && !departure->hasTargetCell && state.relations().empty()
                && findMutable(world.objects, 21)->dockedToObjectId == -1 && world.credits["Player"] == 988,
            hostType + " unrelated completion scope retains legacy release behavior");
    }
    return success;
}

bool runSmoke(const RulesDatabase& rules) {
    bool success = true;
    success &= runAnimationSmoke(rules);
    success &= runBlockedCompletionSmoke();
    const SpecialMechanismRules& repairRules = rules.getSpecialMechanismRules();
    const BuildingType* depotRules = rules.findBuildingType("DEPOT");
    const BuildingType* reloadRules = rules.findBuildingType("RELOADDEPOT");
    const BuildingType* reloadOnlyRules =
        rules.findBuildingType("RELOADONLY");
    const BuildingType* navalDepotRules =
        rules.findBuildingType("NAVYARD");
    const UnitType* tankRules = rules.findUnitType("Vehicle", "TANK");
    success &= expect(
        depotRules != nullptr
            && reloadRules != nullptr
            && tankRules != nullptr
            && depotRules->unitRepair
            && depotRules->numberOfDocks == 2
            && reloadRules->unitRepair
            && reloadRules->unitReload
            && reloadRules->numberOfDocks == 1
            && reloadOnlyRules != nullptr
            && !reloadOnlyRules->unitRepair
            && reloadOnlyRules->unitReload
            && navalDepotRules != nullptr
            && navalDepotRules->unitRepair
            && navalDepotRules->naval
            && navalDepotRules->waterBound
            && !navalDepotRules->capturable
            && repairRules.unitRepairRateMinutes == 0.01
            && repairRules.reloadRateMinutes == 0.02
            && repairRules.repairPercent == 0.15
            && repairRules.repairStep == 8,
        "repair rules fixture"
    );

    {
        SmokeWorld world = makeWorld(rules);
        RuntimeRepairDepotSystemState state(10);
        const RuntimeRepairDepotCommandResult first = state.reserveDock(
            world.context(), 100, 11, 0
        );
        const RuntimeRepairDepotCommandResult second = state.reserveDock(
            world.context(), 100, 10, 0
        );
        world.apply(first.actions);
        world.apply(second.actions);
        success &= expect(
            first.accepted && first.dockIndex == 0
                && second.accepted && second.dockIndex == 1
                && state.relations().size() == 2,
            "two-dock reservation capacity"
        );
        success &= expectVeto(
            state, world, 100, 12, RuntimeRepairDepotVeto::CapacityFull
        );
        success &= expectVeto(
            state, world, 100, 10, RuntimeRepairDepotVeto::UnitAlreadyManaged
        );
        const RuntimeRepairDepotCommandResult wrongArrival = state.attachAtDock(
            world.context(), 100, 10, {999, 999, false}, 0
        );
        success &= expect(
            !wrongArrival.accepted
                && wrongArrival.veto == RuntimeRepairDepotVeto::UnitNotAtDock
                && state.relations().size() == 2,
            "arrival must match reserved dock"
        );

        const RuntimeRepairDepotCommandResult attach11 = state.attachAtDock(
            world.context(),
            100,
            11,
            moveTarget(first),
            0
        );
        const RuntimeRepairDepotCommandResult attach10 = state.attachAtDock(
            world.context(),
            100,
            10,
            moveTarget(second),
            0
        );
        success &= expect(
            attach11.accepted && attach10.accepted,
            "reserved units attach"
        );
        world.apply(attach11.actions);
        world.apply(attach10.actions);

        const std::vector<RuntimeRepairDepotAction> pulse0 = state.tick(
            world.context(), 0
        );
        success &= expect(
            pulse0.size() == 2
                && pulse0[0].kind == RuntimeRepairDepotActionKind::Repair
                && pulse0[0].unitId == 10
                && pulse0[1].kind == RuntimeRepairDepotActionKind::Repair
                && pulse0[1].unitId == 11
                && pulse0[0].healedHitPoints == 8
                && pulse0[0].creditCost == 12
                && pulse0[0].firstRepairPulse
                && pulse0[1].firstRepairPulse,
            "stable host/unit order and multi-unit repair"
        );
        world.apply(pulse0);
        success &= expect(
            findMutable(world.objects, 10)->health == 48
                && findMutable(world.objects, 11)->health == 58
                && world.credits["Player"] == 988
                && world.credits["Ally"] == 988
                && state.remainingServiceTicks(100) == 6,
            "repair pulse effects and URepairRate interval"
        );
        success &= expect(
            state.tick(world.context(), 0).empty()
                && state.remainingServiceTicks(100) == 6
                && state.lastAdvancedTick() == 0,
            "duplicate tick does not advance or repeat service"
        );
        for (int tick = 1; tick < 6; ++tick) {
            success &= expect(
                state.tick(world.context(), tick).empty(),
                "URepairRate cooldown before due tick"
            );
        }
        const std::vector<RuntimeRepairDepotAction> pulse6 = state.tick(
            world.context(), 6
        );
        success &= expect(
            actionCount(pulse6, RuntimeRepairDepotActionKind::Repair) == 2
                && !pulse6.front().firstRepairPulse,
            "multi-unit repair repeats after URepairRate"
        );
    }

    {
        SmokeWorld world = makeWorld(rules);
        world.queueEnabled = true;
        RuntimeRepairDepotSystemState state(10);
        success &= expect(
            reserveAndAttach(state, world, 100, 10, 0)
                && reserveAndAttach(state, world, 100, 11, 0),
            "queue setup occupies every repair dock"
        );

        const RuntimeRepairDepotCommandResult queued19 = state.reserveDock(
            world.context(), 100, 19, 2
        );
        const RuntimeRepairDepotCommandResult queued18 = state.reserveDock(
            world.context(), 100, 18, 3
        );
        const RuntimeRepairDepotCommandResult queued12 = state.reserveDock(
            world.context(), 100, 12, 3
        );
        world.apply(queued19.actions);
        world.apply(queued18.actions);
        world.apply(queued12.actions);
        const RuntimeRepairDepotRelation* relation19 = findRelation(state, 19);
        const RuntimeRepairDepotRelation* relation18 = findRelation(state, 18);
        const RuntimeRepairDepotRelation* relation12 = findRelation(state, 12);
        success &= expect(
            queued19.accepted
                && queued18.accepted
                && queued12.accepted
                && queued19.dockIndex == -1
                && queued19.queueIndex == 0
                && queued18.queueIndex == 1
                && queued12.queueIndex == 2
                && findAction(
                    queued19.actions,
                    RuntimeRepairDepotActionKind::MoveToQueue,
                    19
                ) != nullptr
                && queued19.actions.front().relationPhase
                    == RuntimeRepairDepotRelationPhase::Queued
                && queued19.actions.front().queueIndex == 0
                && relation19 != nullptr
                && relation18 != nullptr
                && relation12 != nullptr
                && relation19->phase
                    == RuntimeRepairDepotRelationPhase::Queued
                && relation18->phase
                    == RuntimeRepairDepotRelationPhase::Queued
                && relation12->phase
                    == RuntimeRepairDepotRelationPhase::Queued
                && relation19->dockIndex == -1
                && relation19->queueCell != relation18->queueCell
                && relation19->queueCell != relation12->queueCell
                && relation18->queueCell != relation12->queueCell,
            "full depot queues without consuming docks or sharing cells"
        );

        std::vector<RuntimeRepairDepotAction> release = state.releaseUnit(
            world.context(), 10, "test frees first dock"
        );
        world.apply(release);
        RuntimeRepairDepotReconcileReport promoted = state.reconcile(
            world.context()
        );
        const RuntimeRepairDepotAction* promote19 = findAction(
            promoted.actions,
            RuntimeRepairDepotActionKind::MoveToDock,
            19
        );
        success &= expect(
            promote19 != nullptr
                && promote19->dockIndex == 0
                && promote19->queueIndex == -1
                && promote19->relationPhase
                    == RuntimeRepairDepotRelationPhase::Reserved
                && findRelation(state, 19) != nullptr
                && findRelation(state, 19)->phase
                    == RuntimeRepairDepotRelationPhase::Reserved,
            "oldest queued request promotes into the first free dock"
        );
        world.apply(promoted.actions);

        release = state.releaseUnit(
            world.context(), 11, "test frees second dock"
        );
        world.apply(release);
        promoted = state.reconcile(world.context());
        const RuntimeRepairDepotAction* promote12 = findAction(
            promoted.actions,
            RuntimeRepairDepotActionKind::MoveToDock,
            12
        );
        success &= expect(
            promote12 != nullptr
                && promote12->dockIndex == 1
                && findRelation(state, 12) != nullptr
                && findRelation(state, 12)->phase
                    == RuntimeRepairDepotRelationPhase::Reserved
                && findRelation(state, 18) != nullptr
                && findRelation(state, 18)->phase
                    == RuntimeRepairDepotRelationPhase::Queued,
            "same-tick queue promotion is stable by unit id"
        );
    }

    {
        SmokeWorld world = makeWorld(rules);
        world.queueEnabled = true;
        world.externallyOccupiedDocks.insert({100, 0});
        world.externallyOccupiedDocks.insert({100, 1});
        RuntimeRepairDepotSystemState state(10);
        const RuntimeRepairDepotCommandResult queued19 = state.reserveDock(
            world.context(), 100, 19, 1
        );
        const RuntimeRepairDepotCommandResult queued18 = state.reserveDock(
            world.context(), 100, 18, 1
        );
        world.apply(queued19.actions);
        world.apply(queued18.actions);
        world.externallyOccupiedDocks.clear();

        RuntimeRepairDepotContext limited = world.context();
        limited.maximumQueuePromotions = 1;
        const RuntimeRepairDepotReconcileReport promoted = state.reconcile(
            limited
        );
        success &= expect(
            queued19.accepted
                && queued18.accepted
                && actionCount(
                    promoted.actions,
                    RuntimeRepairDepotActionKind::MoveToDock
                ) == 1
                && findAction(
                    promoted.actions,
                    RuntimeRepairDepotActionKind::MoveToDock,
                    18
                ) != nullptr
                && findRelation(state, 18) != nullptr
                && findRelation(state, 18)->phase
                    == RuntimeRepairDepotRelationPhase::Reserved
                && findRelation(state, 19) != nullptr
                && findRelation(state, 19)->phase
                    == RuntimeRepairDepotRelationPhase::Queued,
            "one-promotion reconcile budget leaves later queued work queued"
        );
    }

    {
        SmokeWorld world = makeWorld(rules);
        world.queueEnabled = true;
        world.externallyOccupiedDocks.insert({100, 0});
        world.externallyOccupiedDocks.insert({100, 1});
        RuntimeRepairDepotSystemState state(10);
        const RuntimeRepairDepotCommandResult queued19 = state.reserveDock(
            world.context(), 100, 19, 1
        );
        const RuntimeRepairDepotCommandResult queued18 = state.reserveDock(
            world.context(), 100, 18, 1
        );
        world.apply(queued19.actions);
        world.apply(queued18.actions);
        findMutable(world.objects, 16)->health = 96;
        success &= expect(
            queued19.accepted
                && queued18.accepted
                && reserveAndAttach(state, world, 200, 16, 0),
            "shared tick promotion budget setup"
        );
        world.externallyOccupiedDocks.clear();

        RuntimeRepairDepotContext limited = world.context();
        limited.maximumQueuePromotions = 1;
        const std::vector<RuntimeRepairDepotAction> actions = state.tick(
            limited,
            0
        );
        success &= expect(
            actionCount(
                actions,
                RuntimeRepairDepotActionKind::MoveToDock
            ) == 1
                && findAction(
                    actions,
                    RuntimeRepairDepotActionKind::MoveToDock,
                    18
                ) != nullptr
                && findAction(
                    actions,
                    RuntimeRepairDepotActionKind::UndockToRally,
                    16
                ) != nullptr
                && findRelation(state, 18) != nullptr
                && findRelation(state, 18)->phase
                    == RuntimeRepairDepotRelationPhase::Reserved
                && findRelation(state, 19) != nullptr
                && findRelation(state, 19)->phase
                    == RuntimeRepairDepotRelationPhase::Queued
                && findRelation(state, 16) == nullptr,
            "tick completion reconcile shares the exhausted promotion budget"
        );
    }

    {
        SmokeWorld world = makeWorld(rules);
        world.queueEnabled = true;
        world.externallyOccupiedDocks.insert({100, 0});
        world.externallyOccupiedDocks.insert({100, 1});
        RuntimeRepairDepotSystemState state(10);
        const RuntimeRepairDepotCommandResult queued19 = state.reserveDock(
            world.context(), 100, 19, 1
        );
        const RuntimeRepairDepotCommandResult queued18 = state.reserveDock(
            world.context(), 100, 18, 1
        );
        world.apply(queued19.actions);
        world.apply(queued18.actions);
        world.externallyOccupiedDocks.clear();

        const RuntimeRepairDepotReconcileReport promoted = state.reconcile(
            world.context()
        );
        success &= expect(
            queued19.accepted
                && queued18.accepted
                && actionCount(
                    promoted.actions,
                    RuntimeRepairDepotActionKind::MoveToDock
                ) == 2
                && findRelation(state, 18) != nullptr
                && findRelation(state, 18)->phase
                    == RuntimeRepairDepotRelationPhase::Reserved
                && findRelation(state, 19) != nullptr
                && findRelation(state, 19)->phase
                    == RuntimeRepairDepotRelationPhase::Reserved,
            "default reconcile promotion budget preserves unlimited behavior"
        );
    }

    {
        SmokeWorld duplicateCells = makeWorld(rules);
        duplicateCells.queueEnabled = true;
        duplicateCells.collapseQueueCells = true;
        duplicateCells.externallyOccupiedDocks.insert({100, 0});
        duplicateCells.externallyOccupiedDocks.insert({100, 1});
        RuntimeRepairDepotSystemState duplicateState(10);
        const RuntimeRepairDepotCommandResult first =
            duplicateState.reserveDock(
                duplicateCells.context(), 100, 10, 0
            );
        const RuntimeRepairDepotCommandResult second =
            duplicateState.reserveDock(
                duplicateCells.context(), 100, 11, 0
            );
        success &= expect(
            first.accepted
                && first.queueIndex == 0
                && !second.accepted
                && second.veto
                    == RuntimeRepairDepotVeto::QueueCellUnavailable,
            "queue allocator rejects duplicate physical queue cells"
        );

        SmokeWorld bounded = makeWorld(rules);
        bounded.queueEnabled = true;
        bounded.externallyOccupiedDocks.insert({100, 0});
        bounded.externallyOccupiedDocks.insert({100, 1});
        for (int index = 0; index <= 64; ++index) {
            bounded.objects.push_back(makeUnit(
                1000 + index,
                "Player",
                "TANK",
                40
            ));
        }
        RuntimeRepairDepotSystemState boundedState(10);
        bool firstSixtyFourAccepted = true;
        for (int index = 0; index < 64; ++index) {
            firstSixtyFourAccepted =
                boundedState.reserveDock(
                    bounded.context(), 100, 1000 + index, index
                ).accepted
                && firstSixtyFourAccepted;
        }
        const RuntimeRepairDepotCommandResult overflow =
            boundedState.reserveDock(
                bounded.context(), 100, 1064, 64
            );
        success &= expect(
            firstSixtyFourAccepted
                && boundedState.relations().size() == 64
                && !overflow.accepted
                && overflow.veto == RuntimeRepairDepotVeto::CapacityFull,
            "per-host repair queue has a deterministic hard limit"
        );
    }

    {
        const auto makeQueued = [&](SmokeWorld& world) {
            world.queueEnabled = true;
            world.externallyOccupiedDocks.insert({100, 0});
            world.externallyOccupiedDocks.insert({100, 1});
            RuntimeRepairDepotSystemState state(10);
            const RuntimeRepairDepotCommandResult queued = state.reserveDock(
                world.context(), 100, 10, 0
            );
            world.apply(queued.actions);
            return std::pair<RuntimeRepairDepotSystemState,
                             RuntimeRepairDepotCommandResult>{
                std::move(state), queued
            };
        };

        SmokeWorld cancelled = makeWorld(rules);
        auto [cancelledState, cancelledQueue] = makeQueued(cancelled);
        cancelled.cancelledReservations.insert(10);
        RuntimeRepairDepotReconcileReport report = cancelledState.reconcile(
            cancelled.context()
        );
        success &= expect(
            cancelledQueue.accepted
                && report.releasedRelations == 1
                && findAction(
                    report.actions,
                    RuntimeRepairDepotActionKind::Release,
                    10
                ) != nullptr
                && cancelledState.relations().empty(),
            "cancelled queue command releases its relation"
        );

        SmokeWorld pathLost = makeWorld(rules);
        auto [pathLostState, pathLostQueue] = makeQueued(pathLost);
        pathLost.blockedUnits.insert(10);
        report = pathLostState.reconcile(pathLost.context());
        success &= expect(
            pathLostQueue.accepted
                && report.releasedRelations == 1
                && pathLostState.relations().empty(),
            "lost queue path safely releases its relation"
        );

        SmokeWorld promotionPathLost = makeWorld(rules);
        auto [promotionPathState, promotionPathQueue] =
            makeQueued(promotionPathLost);
        promotionPathLost.externallyOccupiedDocks.erase({100, 0});
        promotionPathLost.blockedCellsByUnit.insert({10, 10, 5});
        report = promotionPathState.reconcile(
            promotionPathLost.context()
        );
        success &= expect(
            promotionPathQueue.accepted
                && report.releasedRelations == 1
                && findAction(
                    report.actions,
                    RuntimeRepairDepotActionKind::Release,
                    10
                ) != nullptr
                && promotionPathState.relations().empty(),
            "queue promotion revalidates the exact dock path"
        );

        SmokeWorld promotionDockBusy = makeWorld(rules);
        auto [promotionDockState, promotionDockQueue] =
            makeQueued(promotionDockBusy);
        promotionDockBusy.externallyOccupiedDocks.erase({100, 0});
        promotionDockBusy.unavailableDocks.insert({100, 0});
        report = promotionDockState.reconcile(
            promotionDockBusy.context()
        );
        success &= expect(
            promotionDockQueue.accepted
                && report.releasedRelations == 0
                && report.actions.empty()
                && findRelation(promotionDockState, 10) != nullptr
                && findRelation(promotionDockState, 10)->phase
                    == RuntimeRepairDepotRelationPhase::Queued,
            "temporarily unavailable dock keeps its queue reservation"
        );

        SmokeWorld cellLost = makeWorld(rules);
        auto [cellLostState, cellLostQueue] = makeQueued(cellLost);
        cellLost.unavailableQueueSlots.insert({
            100,
            cellLostQueue.queueIndex
        });
        report = cellLostState.reconcile(cellLost.context());
        success &= expect(
            cellLostQueue.accepted
                && report.releasedRelations == 1
                && cellLostState.relations().empty(),
            "disappearing queue cell safely releases its relation"
        );

        SmokeWorld hostLost = makeWorld(rules);
        auto [hostLostState, hostLostQueue] = makeQueued(hostLost);
        findMutable(hostLost.objects, 100)->alive = false;
        findMutable(hostLost.objects, 100)->health = 0;
        report = hostLostState.reconcile(hostLost.context());
        success &= expect(
            hostLostQueue.accepted
                && report.releasedRelations == 1
                && hostLostState.relations().empty(),
            "destroyed host releases queued vehicles"
        );
    }

    {
        SmokeWorld world = makeWorld(rules);
        world.credits["Player"] = 15;
        findMutable(world.objects, 12)->owner = "player";
        RuntimeRepairDepotSystemState state(10);
        success &= expect(
            reserveAndAttach(state, world, 100, 12, 0)
                && reserveAndAttach(state, world, 100, 10, 0),
            "shared-credit multi-unit attach"
        );
        const std::vector<RuntimeRepairDepotAction> actions = state.tick(
            world.context(), 0
        );
        const RuntimeRepairDepotAction* first = findAction(
            actions, RuntimeRepairDepotActionKind::Repair, 10
        );
        const RuntimeRepairDepotAction* second = findAction(
            actions, RuntimeRepairDepotActionKind::Repair, 12
        );
        success &= expect(
            actions.size() == 2
                && actions[0].unitId == 10
                && actions[1].unitId == 12
                && first != nullptr
                && first->creditCost == 12
                && first->healedHitPoints == 8
                && first->owner == "Player"
                && second != nullptr
                && second->creditCost == 3
                && second->healedHitPoints == 2
                && second->owner == "Player",
            "stable order shares a canonical no-overdraft owner ledger"
        );
        world.apply(actions);
        success &= expect(
            world.credits["Player"] == 0,
            "multi-unit pulse cannot overspend owner balance"
        );
    }

    {
        SmokeWorld world = makeWorld(rules);
        RuntimeRepairDepotSystemState state(10);
        world.credits["Player"] = 5;
        success &= expect(
            reserveAndAttach(state, world, 100, 10, 0),
            "partial-credit unit attach"
        );
        const std::vector<RuntimeRepairDepotAction> actions = state.tick(
            world.context(), 0
        );
        const RuntimeRepairDepotAction* repair = findAction(
            actions, RuntimeRepairDepotActionKind::Repair, 10
        );
        success &= expect(
            repair != nullptr
                && repair->healedHitPoints == 3
                && repair->creditCost == 5
                && repair->healthBefore == 40
                && repair->healthAfter == 43,
            "partial balance repairs affordable HP with reference rounding"
        );
        world.apply(actions);
        success &= expect(world.credits["Player"] == 0, "partial balance spend");
        for (int tick = 1; tick <= 6; ++tick) {
            const std::vector<RuntimeRepairDepotAction> paused = state.tick(
                world.context(), tick
            );
            if (tick == 6) {
                success &= expect(
                    findAction(paused, RuntimeRepairDepotActionKind::Repair) == nullptr,
                    "zero balance pauses repair"
                );
            }
        }

        SmokeWorld oneCredit = makeWorld(rules);
        oneCredit.credits["Player"] = 1;
        RuntimeRepairDepotSystemState oneCreditState(10);
        success &= expect(
            reserveAndAttach(oneCreditState, oneCredit, 100, 10, 0),
            "one-credit unit attach"
        );
        success &= expect(
            oneCreditState.tick(oneCredit.context(), 0).empty()
                && oneCredit.credits["Player"] == 1,
            "unaffordable single HP neither repairs nor spends"
        );
    }

    {
        SmokeWorld world = makeWorld(rules);
        RuntimeRepairDepotSystemState state(10);
        success &= expect(
            reserveAndAttach(state, world, 100, 21, 0),
            "near-full unit attach"
        );
        const std::vector<RuntimeRepairDepotAction> actions = state.tick(
            world.context(), 0
        );
        const RuntimeRepairDepotAction* repair = findAction(
            actions, RuntimeRepairDepotActionKind::Repair, 21
        );
        const RuntimeRepairDepotAction* undock = findAction(
            actions, RuntimeRepairDepotActionKind::UndockToRally, 21
        );
        success &= expect(
            repair != nullptr
                && repair->healedHitPoints == 4
                && repair->creditCost == 12
                && undock != nullptr
                && undock->hasTargetCell
                && state.relations().empty()
                && state.remainingServiceTicks(100) == -1,
            "full repair generates deterministic undock/rally action"
        );
        world.apply(actions);
        success &= expect(
            findMutable(world.objects, 21)->health == 100
                && findMutable(world.objects, 21)->dockedToObjectId == -1,
            "repair then undock action order"
        );
        success &= expect(
            state.reserveDock(world.context(), 100, 10, 1).accepted,
            "completed unit releases capacity for next vehicle"
        );
    }

    {
        SmokeWorld world = makeWorld(rules);
        RuntimeRepairDepotSystemState state(10);
        success &= expect(
            reserveAndAttach(state, world, 200, 10, 0),
            "UnitReload depot attach"
        );
        std::vector<RuntimeRepairDepotAction> actions = state.tick(
            world.context(), 0
        );
        world.apply(actions);
        success &= expect(
            findAction(actions, RuntimeRepairDepotActionKind::Repair) != nullptr
                && state.remainingServiceTicks(200) == 6,
            "UnitRepair always selects URepairRate"
        );
        for (int tick = 1; tick < 6; ++tick) {
            success &= expect(
                state.tick(world.context(), tick).empty(),
                "URepairRate cooldown before due tick on mixed depot"
            );
        }
        actions = state.tick(world.context(), 6);
        success &= expect(
            findAction(actions, RuntimeRepairDepotActionKind::Repair) != nullptr,
            "URepairRate repair due tick on mixed depot"
        );
    }

    {
        SmokeWorld world = makeWorld(rules);
        RuntimeRepairDepotSystemState state(10);
        success &= expect(
            reserveAndAttach(state, world, 100, 10, 0),
            "power pause unit attach"
        );
        findMutable(world.objects, 100)->poweredOperational = false;
        for (int tick = 0; tick < 5; ++tick) {
            success &= expect(
                state.tick(world.context(), tick).empty()
                    && state.remainingServiceTicks(100) == 0,
                "unpowered host pauses initial service"
            );
        }
        findMutable(world.objects, 100)->poweredOperational = true;
        std::vector<RuntimeRepairDepotAction> actions = state.tick(
            world.context(), 5
        );
        success &= expect(
            findAction(actions, RuntimeRepairDepotActionKind::Repair) != nullptr
                && state.remainingServiceTicks(100) == 6,
            "power restore resumes repair"
        );
        world.apply(actions);
        findMutable(world.objects, 100)->poweredOperational = false;
        for (int tick = 6; tick < 16; ++tick) {
            success &= expect(
                state.tick(world.context(), tick).empty()
                    && state.remainingServiceTicks(100) == 6,
                "power loss freezes existing cooldown"
            );
        }
        findMutable(world.objects, 100)->poweredOperational = true;
        for (int tick = 16; tick < 21; ++tick) {
            success &= expect(
                state.tick(world.context(), tick).empty(),
                "restored cooldown has not elapsed"
            );
        }
        actions = state.tick(world.context(), 21);
        success &= expect(
            findAction(actions, RuntimeRepairDepotActionKind::Repair) != nullptr,
            "restored cooldown elapses after powered ticks only"
        );
    }

    {
        SmokeWorld world = makeWorld(rules);
        RuntimeRepairDepotSystemState state(10);
        success &= expect(
            reserveAndAttach(state, world, 100, 10, 0),
            "host destruction relation attach"
        );
        findMutable(world.objects, 100)->alive = false;
        findMutable(world.objects, 100)->health = 0;
        RuntimeRepairDepotReconcileReport report = state.reconcile(
            world.context()
        );
        success &= expect(
            report.releasedRelations == 1
                && report.actions.size() == 1
                && report.actions.front().kind
                    == RuntimeRepairDepotActionKind::Release
                && state.relations().empty(),
            "destroyed host safely releases dock"
        );

        SmokeWorld captured = makeWorld(rules);
        RuntimeRepairDepotSystemState capturedState(10);
        success &= expect(
            reserveAndAttach(capturedState, captured, 100, 10, 0),
            "captured host relation attach"
        );
        findMutable(captured.objects, 100)->owner = "Ally";
        report = capturedState.reconcile(captured.context());
        success &= expect(
            report.releasedRelations == 1
                && capturedState.relations().empty(),
            "host owner change releases relation even when still allied"
        );

        SmokeWorld invalidated = makeWorld(rules);
        RuntimeRepairDepotSystemState invalidatedState(10);
        success &= expect(
            reserveAndAttach(invalidatedState, invalidated, 100, 10, 0),
            "invalidated host relation attach"
        );
        findMutable(invalidated.objects, 100)->type = "NOTADEPOT";
        report = invalidatedState.reconcile(invalidated.context());
        success &= expect(
            report.releasedRelations == 1
                && invalidatedState.relations().empty(),
            "host losing repair capability releases relation"
        );

        SmokeWorld moved = makeWorld(rules);
        RuntimeRepairDepotSystemState movedState(10);
        success &= expect(
            reserveAndAttach(movedState, moved, 100, 10, 0),
            "moved-away unit relation attach"
        );
        ++findMutable(moved.objects, 10)->x;
        report = movedState.reconcile(moved.context());
        success &= expect(
            report.releasedRelations == 1
                && movedState.relations().empty(),
            "unit leaving dock cell cannot receive remote repair"
        );

        SmokeWorld pathLost = makeWorld(rules);
        RuntimeRepairDepotSystemState pathLostState(10);
        const RuntimeRepairDepotCommandResult pathLostReserve =
            pathLostState.reserveDock(pathLost.context(), 100, 10, 0);
        pathLost.blockedUnits.insert(10);
        report = pathLostState.reconcile(pathLost.context());
        success &= expect(
            pathLostReserve.accepted
                && report.releasedRelations == 1
                && pathLostState.relations().empty(),
            "lost repair path releases reserved capacity"
        );

        SmokeWorld cancelled = makeWorld(rules);
        RuntimeRepairDepotSystemState cancelledState(10);
        const RuntimeRepairDepotCommandResult cancelledReserve =
            cancelledState.reserveDock(cancelled.context(), 100, 10, 0);
        cancelled.cancelledReservations.insert(10);
        report = cancelledState.reconcile(cancelled.context());
        success &= expect(
            cancelledReserve.accepted
                && report.releasedRelations == 1
                && cancelledState.relations().empty(),
            "cancelled repair command releases reserved capacity"
        );
    }

    {
        SmokeWorld world = makeWorld(rules);
        RuntimeRepairDepotSystemState state(10);
        success &= expectVeto(
            state, world, 300, 10, RuntimeRepairDepotVeto::HostNotRepairDepot
        );
        success &= expectVeto(
            state, world, 400, 10, RuntimeRepairDepotVeto::HostNotRepairDepot
        );
        success &= expectVeto(
            state, world, 100, 13, RuntimeRepairDepotVeto::UnitNotRepairable
        );
        success &= expectVeto(
            state, world, 100, 14, RuntimeRepairDepotVeto::UnitNotGroundVehicle
        );
        success &= expectVeto(
            state, world, 100, 17, RuntimeRepairDepotVeto::DomainMismatch
        );
        success &= expectVeto(
            state, world, 100, 15, RuntimeRepairDepotVeto::NotAllied
        );
        success &= expectVeto(
            state, world, 100, 16, RuntimeRepairDepotVeto::UnitAlreadyFull
        );

        findMutable(world.objects, 18)->garrisonHostObjectId = 999;
        success &= expectVeto(
            state, world, 100, 18, RuntimeRepairDepotVeto::UnitGarrisoned
        );
        findMutable(world.objects, 19)->dockedToObjectId = 200;
        findMutable(world.objects, 19)->dockIndex = 0;
        success &= expectVeto(
            state, world, 100, 19, RuntimeRepairDepotVeto::UnitAlreadyDocked
        );
        world.transportedUnits.insert(20);
        success &= expectVeto(
            state, world, 100, 20, RuntimeRepairDepotVeto::UnitTransported
        );
        world.blockedUnits.insert(12);
        success &= expectVeto(
            state, world, 100, 12, RuntimeRepairDepotVeto::PathUnavailable
        );

        SmokeWorld externallyFull = makeWorld(rules);
        externallyFull.externallyOccupiedDocks.insert({100, 0});
        externallyFull.externallyOccupiedDocks.insert({100, 1});
        RuntimeRepairDepotSystemState externallyFullState(10);
        success &= expectVeto(
            externallyFullState,
            externallyFull,
            100,
            10,
            RuntimeRepairDepotVeto::CapacityFull
        );

        SmokeWorld spoofedArrival = makeWorld(rules);
        RuntimeRepairDepotSystemState spoofedArrivalState(10);
        const RuntimeRepairDepotCommandResult spoofedReserve =
            spoofedArrivalState.reserveDock(
                spoofedArrival.context(), 100, 10, 0
            );
        const RuntimeRepairDepotCommandResult spoofedAttach =
            spoofedArrivalState.attachAtDock(
                spoofedArrival.context(),
                100,
                10,
                moveTarget(spoofedReserve),
                0
            );
        success &= expect(
            spoofedReserve.accepted
                && !spoofedAttach.accepted
                && spoofedAttach.veto
                    == RuntimeRepairDepotVeto::UnitNotAtDock
                && spoofedArrivalState.relations().size() == 1,
            "attach cannot spoof arrival without matching object position"
        );

        SmokeWorld raced = makeWorld(rules);
        RuntimeRepairDepotSystemState racedState(10);
        const RuntimeRepairDepotCommandResult racedReserve =
            racedState.reserveDock(raced.context(), 100, 10, 0);
        raced.apply(racedReserve.actions);
        raced.externallyOccupiedDocks.insert({
            100,
            racedReserve.dockIndex
        });
        const RuntimeRepairDepotCommandResult racedAttach =
            racedState.attachAtDock(
                raced.context(),
                100,
                10,
                moveTarget(racedReserve),
                0
            );
        const RuntimeRepairDepotAction* racedRelease = findAction(
            racedAttach.actions,
            RuntimeRepairDepotActionKind::Release,
            10
        );
        success &= expect(
            racedReserve.accepted
                && !racedAttach.accepted
                && racedAttach.veto == RuntimeRepairDepotVeto::CapacityFull
                && racedRelease != nullptr
                && racedRelease->hasTargetCell
                && racedState.relations().empty(),
            "attach occupancy race releases unit toward rally"
        );
        raced.apply(racedAttach.actions);
        success &= expect(
            racedRelease != nullptr
                && findMutable(raced.objects, 10)->x
                    == racedRelease->targetCell.x
                && findMutable(raced.objects, 10)->y
                    == racedRelease->targetCell.y,
            "occupancy-race release moves unit out of contested dock cell"
        );

        SmokeWorld fullOnArrival = makeWorld(rules);
        RuntimeRepairDepotSystemState fullOnArrivalState(10);
        const RuntimeRepairDepotCommandResult fullReserve =
            fullOnArrivalState.reserveDock(
                fullOnArrival.context(), 100, 10, 0
            );
        fullOnArrival.apply(fullReserve.actions);
        findMutable(fullOnArrival.objects, 10)->health = 100;
        const RuntimeRepairDepotCommandResult fullAttach =
            fullOnArrivalState.attachAtDock(
                fullOnArrival.context(),
                100,
                10,
                moveTarget(fullReserve),
                0
            );
        const RuntimeRepairDepotAction* fullRally = findAction(
            fullAttach.actions,
            RuntimeRepairDepotActionKind::UndockToRally,
            10
        );
        success &= expect(
            fullReserve.accepted
                && !fullAttach.accepted
                && fullAttach.veto
                    == RuntimeRepairDepotVeto::UnitAlreadyFull
                && fullRally != nullptr
                && fullRally->hasTargetCell
                && fullOnArrivalState.relations().empty(),
            "unit repaired while en route is routed away from dock"
        );
    }

    {
        const std::vector<RuntimeRepairDepotRelationPhase> phases = {
            RuntimeRepairDepotRelationPhase::Queued,
            RuntimeRepairDepotRelationPhase::Reserved,
            RuntimeRepairDepotRelationPhase::Docked
        };
        const std::vector<RuntimeRepairDepotHostTransitionKind> transitions = {
            RuntimeRepairDepotHostTransitionKind::Destroyed,
            RuntimeRepairDepotHostTransitionKind::Sold,
            RuntimeRepairDepotHostTransitionKind::Captured
        };
        for (const RuntimeRepairDepotHostTransitionKind transition
             : transitions) {
            for (const RuntimeRepairDepotRelationPhase phase : phases) {
                SmokeWorld world = makeWorld(rules);
                RuntimeRepairDepotSystemState state(10);
                bool prepared = false;
                if (phase == RuntimeRepairDepotRelationPhase::Queued) {
                    world.queueEnabled = true;
                    world.externallyOccupiedDocks.insert({100, 0});
                    world.externallyOccupiedDocks.insert({100, 1});
                    const RuntimeRepairDepotCommandResult queued =
                        state.reserveDock(world.context(), 100, 10, 0);
                    world.apply(queued.actions);
                    prepared = queued.accepted
                        && queued.queueIndex == 0
                        && !state.claimsDock(100, 0);
                } else if (phase
                        == RuntimeRepairDepotRelationPhase::Reserved) {
                    const RuntimeRepairDepotCommandResult reserved =
                        state.reserveDock(world.context(), 100, 10, 0);
                    prepared = reserved.accepted
                        && state.claimsDock(100, reserved.dockIndex);
                } else {
                    prepared = reserveAndAttach(
                        state,
                        world,
                        100,
                        10,
                        0
                    ) && state.claimsDock(100, 0);
                }

                RuntimeRepairDepotHostTransitionRequest request;
                request.hostId = 100;
                request.kind = transition;
                request.expectedOwner = "Player";
                request.newOwner = transition
                        == RuntimeRepairDepotHostTransitionKind::Captured
                    ? "Ally"
                    : std::string();
                const RuntimeRepairDepotHostTransitionResult result =
                    state.transitionHost(world.context(), request);
                const RuntimeRepairDepotAction* action =
                    result.actions.empty() ? nullptr : &result.actions.front();
                const bool waiting = phase
                    != RuntimeRepairDepotRelationPhase::Docked;
                RuntimeRepairDepotActionKind expectedKind =
                    RuntimeRepairDepotActionKind::Release;
                if (!waiting) {
                    expectedKind = transition
                            == RuntimeRepairDepotHostTransitionKind::Destroyed
                        ? RuntimeRepairDepotActionKind::DestroyDockedUnit
                        : transition
                                == RuntimeRepairDepotHostTransitionKind::Sold
                            ? RuntimeRepairDepotActionKind::SellDockedUnit
                            : RuntimeRepairDepotActionKind::TransferDockedUnitOwner;
                }
                const bool retained = !waiting
                    && transition
                        == RuntimeRepairDepotHostTransitionKind::Captured;
                success &= expect(
                    prepared
                        && result.accepted
                        && result.veto == RuntimeRepairDepotVeto::None
                        && result.actions.size() == 1
                        && action != nullptr
                        && action->kind == expectedKind
                        && action->domain
                            == RuntimeRepairDepotDomain::Ground
                        && action->expectedOwner == "Player"
                        && (expectedKind
                                != RuntimeRepairDepotActionKind::SellDockedUnit
                            || action->refundValue == 500)
                        && (expectedKind
                                != RuntimeRepairDepotActionKind::TransferDockedUnitOwner
                            || action->newOwner == "Ally")
                        && (retained
                            ? state.relations().size() == 1
                                && state.relations().front().hostOwner == "Ally"
                                && state.relations().front().unitOwner == "Ally"
                            : state.relations().empty())
                        && (!retained || state.claimsDock(100, 0))
                        && (retained || !state.claimsDock(100, 0)),
                    "phase-by-lifecycle transition matrix"
                );
                if (prepared && result.accepted && !waiting) {
                    const int creditsBefore = world.credits["Player"];
                    world.apply(result.actions);
                    const WorldObjectInstance* transitionedUnit =
                        findMutable(world.objects, 10);
                    success &= expect(
                        transitionedUnit != nullptr
                            && (transition
                                    == RuntimeRepairDepotHostTransitionKind::Destroyed
                                ? !transitionedUnit->alive
                                : transition
                                        == RuntimeRepairDepotHostTransitionKind::Sold
                                    ? !transitionedUnit->alive
                                        && world.credits["Player"]
                                            == creditsBefore + 500
                                    : transitionedUnit->alive
                                        && transitionedUnit->owner == "Ally"),
                        "docked lifecycle action applies destruction, refund or transfer"
                    );
                }
            }
        }
    }

    {
        SmokeWorld world = makeWorld(rules);
        RuntimeRepairDepotSystemState state(10);
        success &= expect(
            reserveAndAttach(state, world, 100, 10, 0),
            "temporal destruction setup"
        );
        RuntimeRepairDepotHostTransitionRequest request;
        request.hostId = 100;
        request.kind = RuntimeRepairDepotHostTransitionKind::Destroyed;
        request.temporalRemoval = true;
        request.expectedOwner = "Player";
        const RuntimeRepairDepotHostTransitionResult result =
            state.transitionHost(world.context(), request);
        success &= expect(
            result.accepted
                && result.actions.size() == 1
                && result.actions.front().kind
                    == RuntimeRepairDepotActionKind::Release
                && result.actions.front().hasTargetCell
                && state.relations().empty(),
            "temporal host removal releases docked ground unit"
        );
        world.apply(result.actions);
        success &= expect(
            findMutable(world.objects, 10)->alive
                && findMutable(world.objects, 10)->health == 40
                && findMutable(world.objects, 10)->dockedToObjectId == -1,
            "temporal host removal preserves unit"
        );
    }

    {
        SmokeWorld world = makeWorld(rules);
        RuntimeRepairDepotSystemState state(10);
        success &= expect(
            reserveAndAttach(state, world, 100, 10, 0)
                && reserveAndAttach(state, world, 100, 11, 0),
            "atomic sale preflight setup"
        );
        world.refunds.erase(11);
        const std::uint64_t before = state.stateHash();
        RuntimeRepairDepotHostTransitionRequest request;
        request.hostId = 100;
        request.kind = RuntimeRepairDepotHostTransitionKind::Sold;
        request.expectedOwner = "Player";
        const RuntimeRepairDepotHostTransitionResult result =
            state.transitionHost(world.context(), request);
        success &= expect(
            !result.accepted
                && result.veto == RuntimeRepairDepotVeto::RefundUnavailable
                && result.actions.empty()
                && state.stateHash() == before
                && state.relations().size() == 2
                && state.claimsDock(100, 0)
                && state.claimsDock(100, 1),
            "sale preflight failure leaves all core state unchanged"
        );
    }

    {
        SmokeWorld world = makeWorld(rules);
        RuntimeRepairDepotSystemState state(10);
        success &= expectVeto(
            state,
            world,
            500,
            10,
            RuntimeRepairDepotVeto::DomainMismatch
        );
        success &= expectVeto(
            state,
            world,
            100,
            22,
            RuntimeRepairDepotVeto::DomainMismatch
        );

        world.queueEnabled = true;
        success &= expect(
            reserveAndAttach(state, world, 500, 22, 0)
                && reserveAndAttach(state, world, 500, 24, 0),
            "naval repair docks accept authored naval units"
        );
        const RuntimeRepairDepotCommandResult queued = state.reserveDock(
            world.context(), 500, 25, 1
        );
        world.apply(queued.actions);
        success &= expect(
            queued.accepted
                && queued.queueIndex == 0
                && queued.actions.front().domain
                    == RuntimeRepairDepotDomain::Naval
                && !state.claimsDock(500, queued.dockIndex),
            "naval repair queue preserves domain without claiming a dock"
        );
        world.apply(state.releaseUnit(
            world.context(),
            22,
            "naval queue promotion setup"
        ));
        const RuntimeRepairDepotReconcileReport promoted = state.reconcile(
            world.context()
        );
        const RuntimeRepairDepotAction* navalMove = findAction(
            promoted.actions,
            RuntimeRepairDepotActionKind::MoveToDock,
            25
        );
        success &= expect(
            navalMove != nullptr
                && navalMove->domain == RuntimeRepairDepotDomain::Naval
                && state.claimsDock(500, navalMove->dockIndex),
            "naval queue promotes into a released naval dock"
        );
    }

    {
        SmokeWorld world = makeWorld(rules);
        RuntimeRepairDepotSystemState state(10);
        success &= expect(
            reserveAndAttach(state, world, 500, 23, 0),
            "naval repair and rally setup"
        );
        std::ostringstream navalSaved;
        state.save(navalSaved);
        IniFile navalIni;
        RuntimeRepairDepotSystemState navalRestored;
        std::string navalLoadError;
        success &= expect(
            navalIni.loadFromString(navalSaved.str())
                && navalRestored.load(
                    navalIni,
                    world.context(),
                    &navalLoadError
                )
                && navalLoadError.empty()
                && navalRestored.stateHash() == state.stateHash()
                && navalRestored.relations().size() == 1
                && navalRestored.relations().front().domain
                    == RuntimeRepairDepotDomain::Naval,
            "schema-3 roundtrip preserves naval relation and hash"
        );
        const std::vector<RuntimeRepairDepotAction> actions = state.tick(
            world.context(), 0
        );
        const RuntimeRepairDepotAction* repair = findAction(
            actions,
            RuntimeRepairDepotActionKind::Repair,
            23
        );
        const RuntimeRepairDepotAction* rally = findAction(
            actions,
            RuntimeRepairDepotActionKind::UndockToRally,
            23
        );
        success &= expect(
            repair != nullptr
                && repair->domain == RuntimeRepairDepotDomain::Naval
                && rally != nullptr
                && rally->domain == RuntimeRepairDepotDomain::Naval
                && rally->hasTargetCell
                && state.relations().empty(),
            "naval repair completion releases to rally"
        );
        world.apply(actions);
        success &= expect(
            findMutable(world.objects, 23)->alive
                && findMutable(world.objects, 23)->health == 100
                && findMutable(world.objects, 23)->dockedToObjectId == -1,
            "naval repair/rally actions preserve the unit"
        );
    }

    {
        for (const RuntimeRepairDepotHostTransitionKind transition : {
                RuntimeRepairDepotHostTransitionKind::Destroyed,
                RuntimeRepairDepotHostTransitionKind::Sold}) {
            SmokeWorld world = makeWorld(rules);
            RuntimeRepairDepotSystemState state(10);
            success &= expect(
                reserveAndAttach(state, world, 500, 22, 0),
                "naval lifecycle setup"
            );
            RuntimeRepairDepotHostTransitionRequest request;
            request.hostId = 500;
            request.kind = transition;
            request.expectedOwner = "Player";
            const RuntimeRepairDepotHostTransitionResult result =
                state.transitionHost(world.context(), request);
            success &= expect(
                result.accepted
                    && result.actions.size() == 1
                    && (result.actions.front().kind
                            == RuntimeRepairDepotActionKind::UndockToRally
                        || result.actions.front().kind
                            == RuntimeRepairDepotActionKind::Release)
                    && result.actions.front().domain
                        == RuntimeRepairDepotDomain::Naval
                    && result.actions.front().refundValue == 0
                    && state.relations().empty(),
                "destroyed/sold naval yard releases docked unit"
            );
            world.apply(result.actions);
            success &= expect(
                findMutable(world.objects, 22)->alive
                    && findMutable(world.objects, 22)->health == 40,
                "naval yard lifecycle never destroys or sells docked unit"
            );
        }
    }

    {
        // Stock naval yards are authored Capturable=no in the fixture, but
        // the core supports capture for mods and scripted ownership changes.
        SmokeWorld world = makeWorld(rules);
        world.credits["Player"] = 0;
        world.credits["Ally"] = 1000;
        RuntimeRepairDepotSystemState state(10);
        success &= expect(
            reserveAndAttach(state, world, 500, 22, 0),
            "naval capture transfer setup"
        );
        RuntimeRepairDepotHostTransitionRequest request;
        request.hostId = 500;
        request.kind = RuntimeRepairDepotHostTransitionKind::Captured;
        request.expectedOwner = "Player";
        request.newOwner = "Ally";
        const RuntimeRepairDepotHostTransitionResult result =
            state.transitionHost(world.context(), request);
        world.apply(result.actions);
        findMutable(world.objects, 500)->owner = "Ally";
        const std::vector<RuntimeRepairDepotAction> pulse = state.tick(
            world.context(), 0
        );
        const RuntimeRepairDepotAction* repair = findAction(
            pulse,
            RuntimeRepairDepotActionKind::Repair,
            22
        );
        success &= expect(
            result.accepted
                && result.actions.size() == 1
                && result.actions.front().kind
                    == RuntimeRepairDepotActionKind::TransferDockedUnitOwner
                && result.actions.front().newOwner == "Ally"
                && findMutable(world.objects, 22)->owner == "Ally"
                && state.relations().size() == 1
                && state.relations().front().hostOwner == "Ally"
                && state.relations().front().unitOwner == "Ally"
                && repair != nullptr
                && repair->owner == "Ally"
                && repair->creditCost == 12,
            "capture transfer uses the new owner ledger on the next pulse"
        );
    }

    {
        SmokeWorld world = makeWorld(rules);
        world.queueEnabled = true;
        world.externallyOccupiedDocks.insert({100, 0});
        world.externallyOccupiedDocks.insert({100, 1});
        RuntimeRepairDepotSystemState source(10);
        const RuntimeRepairDepotCommandResult queued = source.reserveDock(
            world.context(), 100, 10, 4
        );
        world.apply(queued.actions);
        const std::uint64_t queuedHash = source.stateHash();
        std::ostringstream queuedSave;
        source.save(queuedSave);
        IniFile queuedIni;
        success &= expect(
            queued.accepted
                && queued.queueIndex == 0
                && queuedIni.loadFromString(queuedSave.str()),
            "mid-queue save parses"
        );
        RuntimeRepairDepotSystemState restored;
        RuntimeRepairDepotReconcileReport loadReport;
        std::string loadError;
        success &= expect(
            restored.load(
                queuedIni,
                world.context(),
                &loadError,
                &loadReport
            )
                && loadReport.retainedRelations == 1
                && loadReport.releasedRelations == 0
                && restored.stateHash() == queuedHash
                && findRelation(restored, 10) != nullptr
                && findRelation(restored, 10)->phase
                    == RuntimeRepairDepotRelationPhase::Queued
                && findRelation(restored, 10)->queueIndex == 0,
            "mid-queue roundtrip preserves relation and hash"
        );

        world.externallyOccupiedDocks.clear();
        RuntimeRepairDepotContext strictLoadContext = world.context();
        strictLoadContext.allowQueuePromotion = false;
        RuntimeRepairDepotSystemState strictRestored;
        RuntimeRepairDepotReconcileReport strictLoadReport;
        loadError.clear();
        success &= expect(
            strictRestored.load(
                queuedIni,
                strictLoadContext,
                &loadError,
                &strictLoadReport
            )
                && loadError.empty()
                && strictLoadReport.retainedRelations == 1
                && strictLoadReport.releasedRelations == 0
                && strictLoadReport.actions.empty()
                && strictRestored.stateHash() == queuedHash
                && findRelation(strictRestored, 10) != nullptr
                && findRelation(strictRestored, 10)->phase
                    == RuntimeRepairDepotRelationPhase::Queued
                && findRelation(strictRestored, 10)->queueIndex == 0,
            "strict load reconcile preserves a queued relation without actions"
        );
        world.externallyOccupiedDocks.insert({100, 0});
        world.externallyOccupiedDocks.insert({100, 1});

        RuntimeRepairDepotSystemState victim;
        success &= expect(
            victim.load(queuedIni, world.context()),
            "queued corrupt-load victim baseline"
        );
        const std::uint64_t victimHash = victim.stateHash();
        std::string invalidQueueText = queuedSave.str();
        replaceFirst(invalidQueueText, "QueueIndex=0", "QueueIndex=64");
        IniFile invalidQueueIni;
        invalidQueueIni.loadFromString(invalidQueueText);
        loadError.clear();
        success &= expect(
            !victim.load(
                invalidQueueIni,
                world.context(),
                &loadError
            )
                && !loadError.empty()
                && victim.stateHash() == victimHash,
            "invalid queued save is rejected atomically"
        );
        std::string invalidDomainText = queuedSave.str();
        replaceFirst(
            invalidDomainText,
            "Domain=Ground",
            "Domain=Space"
        );
        IniFile invalidDomainIni;
        invalidDomainIni.loadFromString(invalidDomainText);
        loadError.clear();
        success &= expect(
            !victim.load(
                invalidDomainIni,
                world.context(),
                &loadError
            )
                && !loadError.empty()
                && victim.stateHash() == victimHash,
            "invalid schema-3 domain is rejected atomically"
        );

        SmokeWorld legacyWorld = makeWorld(rules);
        RuntimeRepairDepotSystemState legacySource(10);
        const RuntimeRepairDepotCommandResult legacyReserve =
            legacySource.reserveDock(
                legacyWorld.context(), 100, 12, 3
            );
        legacyWorld.apply(legacyReserve.actions);
        std::ostringstream versionThreeSource;
        legacySource.save(versionThreeSource);
        std::istringstream versionTwoLines(versionThreeSource.str());
        std::ostringstream versionOne;
        std::ostringstream versionTwo;
        std::string line;
        while (std::getline(versionTwoLines, line)) {
            if (line.rfind("Domain=", 0) != 0) {
                if (line == "Version=4") {
                    versionTwo << "Version=2\n";
                } else {
                    versionTwo << line << '\n';
                }
            }
            if (line.rfind("Queue", 0) == 0
                || line.rfind("Domain=", 0) == 0) {
                continue;
            }
            if (line == "Version=4") {
                line = "Version=1";
            }
            versionOne << line << '\n';
        }
        IniFile versionOneIni;
        RuntimeRepairDepotSystemState legacyRestored;
        success &= expect(
            legacyReserve.accepted
                && versionOneIni.loadFromString(versionOne.str())
                && legacyRestored.load(
                    versionOneIni,
                    legacyWorld.context(),
                    &loadError,
                    &loadReport
                )
                && loadReport.retainedRelations == 1
                && findRelation(legacyRestored, 12) != nullptr
                && findRelation(legacyRestored, 12)->phase
                    == RuntimeRepairDepotRelationPhase::Reserved
                && findRelation(legacyRestored, 12)->queueIndex == -1,
            "schema-1 reserved relation remains load-compatible"
        );
        IniFile versionTwoIni;
        RuntimeRepairDepotSystemState versionTwoRestored;
        success &= expect(
            versionTwoIni.loadFromString(versionTwo.str())
                && versionTwoRestored.load(
                    versionTwoIni,
                    legacyWorld.context(),
                    &loadError,
                    &loadReport
                )
                && loadReport.retainedRelations == 1
                && findRelation(versionTwoRestored, 12) != nullptr
                && findRelation(versionTwoRestored, 12)->domain
                    == RuntimeRepairDepotDomain::Ground
                && versionTwoRestored.stateHash()
                    == legacySource.stateHash(),
            "schema-2 relation migrates deterministically to Ground"
        );
    }

    {
        SmokeWorld world = makeWorld(rules);
        RuntimeRepairDepotSystemState source(10);
        success &= expect(
            reserveAndAttach(source, world, 100, 10, 3),
            "save source relation attach"
        );
        std::vector<RuntimeRepairDepotAction> firstPulse = source.tick(
            world.context(), 3
        );
        world.apply(firstPulse);
        const std::uint64_t savedHash = source.stateHash();
        std::ostringstream saved;
        source.save(saved);
        IniFile savedIni;
        success &= expect(
            savedIni.loadFromString(saved.str()),
            "repair state save parses"
        );
        RuntimeRepairDepotSystemState restored;
        RuntimeRepairDepotReconcileReport loadReport;
        std::string loadError;
        success &= expect(
            restored.load(
                savedIni,
                world.context(),
                &loadError,
                &loadReport
            )
                && loadReport.retainedRelations == 1
                && loadReport.releasedRelations == 0
                && restored.stateHash() == savedHash
                && restored.remainingServiceTicks(100) == 6,
            "mid-repair save/load/hash consistency"
        );
        RuntimeRepairDepotSystemState deterministic;
        success &= expect(
            deterministic.load(savedIni, world.context())
                && deterministic.stateHash() == restored.stateHash(),
            "deterministic repeated load"
        );

        IniFile legacy;
        legacy.loadFromString("[OtherRuntime]\nVersion=1\n");
        loadError = "stale error";
        success &= expect(
            restored.load(legacy, world.context(), &loadError)
                && restored.relations().empty()
                && restored.remainingServiceTicks(100) == -1
                && restored.lastAdvancedTick() == -1
                && loadError.empty(),
            "legacy save without section resets to empty"
        );

        std::string corruptText = saved.str();
        const std::size_t relationStart = corruptText.find(
            "[RepairDepotRuntime.Relation.0]"
        );
        const std::size_t timerStart = corruptText.find(
            "[RepairDepotRuntime.Timer.0]"
        );
        std::string duplicateRelation;
        if (relationStart != std::string::npos
            && timerStart != std::string::npos
            && relationStart < timerStart) {
            duplicateRelation = corruptText.substr(
                relationStart,
                timerStart - relationStart
            );
            replaceFirst(
                duplicateRelation,
                "RepairDepotRuntime.Relation.0",
                "RepairDepotRuntime.Relation.1"
            );
            corruptText += duplicateRelation;
            replaceFirst(corruptText, "RelationCount=1", "RelationCount=2");
        }
        IniFile corruptIni;
        corruptIni.loadFromString(corruptText);
        RuntimeRepairDepotSystemState victim;
        success &= expect(
            victim.load(savedIni, world.context()),
            "corrupt-load victim baseline"
        );
        const std::uint64_t victimHash = victim.stateHash();
        loadError.clear();
        success &= expect(
            !duplicateRelation.empty()
                && !victim.load(corruptIni, world.context(), &loadError)
                && victim.stateHash() == victimHash
                && !loadError.empty(),
            "duplicate/corrupt save rejected atomically"
        );

        IniFile oversizedIni;
        oversizedIni.loadFromString(
            "[RepairDepotRuntime]\n"
            "Version=1\n"
            "FixedTickHz=10\n"
            "LastAdvancedTick=-1\n"
            "RelationCount=4097\n"
            "HostTimerCount=0\n"
        );
        loadError.clear();
        success &= expect(
            !victim.load(oversizedIni, world.context(), &loadError)
                && victim.stateHash() == victimHash
                && !loadError.empty(),
            "oversized relation save is rejected atomically"
        );

        SmokeWorld staleWorld = world;
        findMutable(staleWorld.objects, 100)->alive = false;
        findMutable(staleWorld.objects, 100)->health = 0;
        RuntimeRepairDepotSystemState staleLoaded;
        RuntimeRepairDepotReconcileReport staleReport;
        success &= expect(
            staleLoaded.load(
                savedIni,
                staleWorld.context(),
                &loadError,
                &staleReport
            )
                && staleReport.releasedRelations == 1
                && staleLoaded.relations().empty()
                && loadError.empty(),
            "stale saved relation is safely filtered"
        );
    }

    return success;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr
            << "usage: concord_runtime_repair_depot_smoke <rules-fixture>\n";
        return 2;
    }
    IniFile ini;
    RulesDatabase rules;
    if (!ini.loadFromFile(argv[1]) || !rules.loadFromIni(ini)) {
        std::cerr << "repair depot rules fixture failed to load\n";
        return 1;
    }
    return runSmoke(rules) ? 0 : 1;
}
