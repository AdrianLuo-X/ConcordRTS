#include "simulation/buildings/RuntimeGateSystem.h"

#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "gate system smoke failure: " << message << '\n';
    }
    return condition;
}

WorldObjectInstance makeObject(
    int id,
    WorldObjectKind kind,
    const std::string& owner,
    const std::string& type
) {
    WorldObjectInstance object;
    object.instanceId = id;
    object.kind = kind;
    object.owner = owner;
    object.type = type;
    object.health = kind == WorldObjectKind::Structure ? 1000 : 100;
    object.alive = true;
    object.x = id % 17;
    object.y = id % 13;
    object.mission = "Guard";
    return object;
}

WorldObjectInstance* findMutable(
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

const RuntimeGateAction* findAction(
    const std::vector<RuntimeGateAction>& actions,
    RuntimeGateActionKind kind,
    int gateId
) {
    for (const RuntimeGateAction& action : actions) {
        if (action.kind == kind && action.gateId == gateId) {
            return &action;
        }
    }
    return nullptr;
}

struct SmokeWorld {
    const RulesDatabase* rules = nullptr;
    std::vector<WorldObjectInstance> objects;
    std::vector<RuntimeGatePassageRequest> requests;
    std::set<int> occupiedGateIds;

    RuntimeGateContext context() {
        RuntimeGateContext result;
        result.rules = rules;
        result.objects = &objects;
        result.passageRequests = &requests;
        result.areAllied = [](const std::string& left,
                              const std::string& right) {
            const bool leftFriendly = left == "Player" || left == "Ally";
            const bool rightFriendly = right == "Player" || right == "Ally";
            return leftFriendly && rightFriendly;
        };
        result.doorwayOccupied = [this](int gateId) {
            return occupiedGateIds.find(gateId)
                != occupiedGateIds.end();
        };
        return result;
    }
};

SmokeWorld makeWorld(const RulesDatabase& rules) {
    SmokeWorld world;
    world.rules = &rules;
    world.objects.push_back(makeObject(
        101,
        WorldObjectKind::Structure,
        "Player",
        "GATEB"
    ));
    world.objects.push_back(makeObject(
        100,
        WorldObjectKind::Structure,
        "Player",
        "GATEA"
    ));
    world.objects.push_back(makeObject(
        200,
        WorldObjectKind::Infantry,
        "Ally",
        "E1"
    ));
    world.objects.push_back(makeObject(
        300,
        WorldObjectKind::Unit,
        "Enemy",
        "TANK"
    ));
    world.objects.push_back(makeObject(
        301,
        WorldObjectKind::Unit,
        "Enemy",
        "BOAT"
    ));
    WorldObjectInstance plane = makeObject(
        400,
        WorldObjectKind::Unit,
        "Ally",
        "PLANE"
    );
    plane.authoredAircraft = true;
    plane.airborne = true;
    world.objects.push_back(std::move(plane));
    return world;
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

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: concord_gate_system_smoke <rules.ini>\n";
        return 2;
    }

    IniFile ini;
    if (!ini.loadFromFile(argv[1])) {
        std::cerr << "gate system smoke failure: fixture load failed\n";
        return 3;
    }
    RulesDatabase rules;
    if (!rules.loadFromIni(ini)) {
        std::cerr << "gate system smoke failure: rules load failed\n";
        return 4;
    }

    bool ok = true;
    SmokeWorld world = makeWorld(rules);
    RuntimeGateContext context = world.context();
    RuntimeGateSystemState state({10, 2, 2});

    // Reconciliation discovers live gates in stable instance-id order.
    std::vector<RuntimeGateAction> actions = state.tick(context, 0);
    ok &= expect(actions.empty(), "initial closed gates emitted actions");
    ok &= expect(state.gates().size() == 2, "live gates were not discovered");
    if (state.gates().size() == 2) {
        ok &= expect(
            state.gates()[0].gateId == 100
                && state.gates()[1].gateId == 101,
            "gate state is not sorted by instance id"
        );
    }
    ok &= expect(
        state.closeDelayTicks(context, 100) == 3,
        "GateCloseDelay minutes were not converted to fixed ticks"
    );

    // Authored DeployTime is minutes.  Stock-like .044 at 30 Hz is 79.2
    // ticks and therefore takes 80 ticks after tolerant ceil.  The full span
    // is retained at mid-transition and across save/load.
    {
        SmokeWorld stockWorld;
        stockWorld.rules = &rules;
        stockWorld.objects = {
            makeObject(
                500,
                WorldObjectKind::Structure,
                "Player",
                "GATESTOCK"
            ),
            makeObject(501, WorldObjectKind::Infantry, "Ally", "E1")
        };
        stockWorld.requests = {{500, 501}};
        RuntimeGateContext stockContext = stockWorld.context();
        RuntimeGateSystemState stockState({30, 7, 9});
        actions = stockState.tick(stockContext, 0);
        const RuntimeGateInstanceState* stockGate = stockState.findGate(500);
        ok &= expect(
            findAction(
                actions,
                RuntimeGateActionKind::BeginOpening,
                500
            ) != nullptr
                && stockGate != nullptr
                && stockGate->phaseStartedTick == 0
                && stockGate->phaseCompletesTick == 80,
            "DeployTime .044 at 30 Hz did not ceil to an 80-tick opening"
        );

        actions = stockState.tick(stockContext, 40);
        stockGate = stockState.findGate(500);
        ok &= expect(
            actions.empty()
                && stockGate != nullptr
                && stockGate->phase == RuntimeGatePhase::Opening
                && stockGate->phaseCompletesTick
                    - stockGate->phaseStartedTick == 80
                && (40 - stockGate->phaseStartedTick) * 2 == 80,
            "authored opening did not retain continuous half-stage progress"
        );

        std::ostringstream midSavedStream;
        stockState.save(midSavedStream);
        IniFile midSavedIni;
        ok &= expect(
            midSavedIni.loadFromString(midSavedStream.str()),
            "mid-transition gate save did not parse"
        );
        RuntimeGateSystemState midRestored;
        std::string midError;
        ok &= expect(
            midRestored.load(midSavedIni, stockContext, &midError, nullptr)
                && midRestored.stateHash() == stockState.stateHash(),
            "mid-transition gate round-trip changed timing/hash: " + midError
        );

        actions = midRestored.tick(stockContext, 79);
        ok &= expect(
            actions.empty()
                && midRestored.phaseOf(500) == RuntimeGatePhase::Opening,
            "authored gate completed before tick 80"
        );
        actions = midRestored.tick(stockContext, 80);
        ok &= expect(
            findAction(actions, RuntimeGateActionKind::FullyOpen, 500)
                != nullptr
                && midRestored.phaseOf(500) == RuntimeGatePhase::Open,
            "authored gate did not complete at tick 80"
        );

        stockWorld.requests.clear();
        actions = midRestored.tick(stockContext, 81);
        stockGate = midRestored.findGate(500);
        ok &= expect(
            findAction(actions, RuntimeGateActionKind::BeginClosing, 500)
                != nullptr
                && stockGate != nullptr
                && stockGate->phaseStartedTick == 81
                && stockGate->phaseCompletesTick == 161,
            "authored closing did not reuse the 80-tick DeployTime"
        );
        (void)midRestored.tick(stockContext, 100);
        stockWorld.occupiedGateIds.insert(500);
        const RuntimeGateInstanceState* beforeReverse =
            midRestored.findGate(500);
        const int beforeStart = beforeReverse == nullptr
            ? -1
            : beforeReverse->phaseStartedTick;
        const int beforeComplete = beforeReverse == nullptr
            ? -1
            : beforeReverse->phaseCompletesTick;
        actions = midRestored.tick(stockContext, 101);
        stockGate = midRestored.findGate(500);
        const int beforeOpenNumerator = beforeComplete - 101;
        const int beforeDuration = beforeComplete - beforeStart;
        const int afterOpenNumerator = stockGate == nullptr
            ? -1
            : 101 - stockGate->phaseStartedTick;
        const int afterDuration = stockGate == nullptr
            ? -1
            : stockGate->phaseCompletesTick - stockGate->phaseStartedTick;
        ok &= expect(
            findAction(
                actions,
                RuntimeGateActionKind::BeginOpening,
                500
            ) != nullptr
                && stockGate != nullptr
                && beforeOpenNumerator * afterDuration
                    == afterOpenNumerator * beforeDuration
                && stockGate->phaseStartedTick == 41
                && stockGate->phaseCompletesTick == 121
                && stockGate->phaseCompletesTick - 101 == 20,
            "closing reversal jumped stage or reopened more than the closed part"
        );

        std::ostringstream reverseSavedStream;
        midRestored.save(reverseSavedStream);
        IniFile reverseSavedIni;
        ok &= expect(
            reverseSavedIni.loadFromString(reverseSavedStream.str()),
            "reversed mid-transition save did not parse"
        );
        RuntimeGateSystemState reverseRestored;
        std::string reverseError;
        ok &= expect(
            reverseRestored.load(
                reverseSavedIni,
                stockContext,
                &reverseError,
                nullptr
            )
                && reverseRestored.stateHash() == midRestored.stateHash(),
            "reversed transition round-trip changed timing/hash: "
                + reverseError
        );
        actions = reverseRestored.tick(stockContext, 120);
        ok &= expect(
            actions.empty()
                && reverseRestored.phaseOf(500) == RuntimeGatePhase::Opening,
            "reversed gate completed before its closed portion was restored"
        );
        actions = reverseRestored.tick(stockContext, 121);
        ok &= expect(
            findAction(actions, RuntimeGateActionKind::FullyOpen, 500)
                != nullptr
                && reverseRestored.phaseOf(500) == RuntimeGatePhase::Open,
            "reversed gate did not finish after exactly 20 ticks"
        );
    }

    // A missing/non-positive DeployTime retains the configured legacy
    // opening and closing durations independently.
    {
        SmokeWorld fallbackWorld;
        fallbackWorld.rules = &rules;
        fallbackWorld.objects = {
            makeObject(
                600,
                WorldObjectKind::Structure,
                "Player",
                "GATEA"
            ),
            makeObject(601, WorldObjectKind::Infantry, "Ally", "E1")
        };
        fallbackWorld.requests = {{600, 601}};
        RuntimeGateContext fallbackContext = fallbackWorld.context();
        RuntimeGateSystemState fallbackState({30, 7, 9});
        actions = fallbackState.tick(fallbackContext, 0);
        const RuntimeGateInstanceState* fallbackGate =
            fallbackState.findGate(600);
        ok &= expect(
            fallbackGate != nullptr
                && fallbackGate->phaseStartedTick == 0
                && fallbackGate->phaseCompletesTick == 7,
            "non-positive DeployTime did not use openingTicks fallback"
        );
        actions = fallbackState.tick(fallbackContext, 7);
        ok &= expect(
            findAction(actions, RuntimeGateActionKind::FullyOpen, 600)
                != nullptr,
            "fallback opening did not complete at openingTicks"
        );
        fallbackWorld.requests.clear();
        actions = fallbackState.tick(fallbackContext, 16);
        fallbackGate = fallbackState.findGate(600);
        ok &= expect(
            findAction(actions, RuntimeGateActionKind::BeginClosing, 600)
                != nullptr
                && fallbackGate != nullptr
                && fallbackGate->phaseStartedTick == 16
                && fallbackGate->phaseCompletesTick == 25,
            "non-positive DeployTime did not use closingTicks fallback"
        );
    }

    // An enemy, aircraft, or naval mover cannot trigger a closed gate.
    world.requests = {{100, 300}, {100, 301}, {100, 400}};
    actions = state.tick(context, 1);
    ok &= expect(actions.empty(), "enemy/non-ground request opened a gate");
    ok &= expect(
        state.phaseOf(100) == RuntimeGatePhase::Closed,
        "enemy request changed the closed phase"
    );

    // An allied ground mover starts opening.  A duplicate same-tick call is
    // a strict no-op, including state hash and action output.
    world.requests = {{100, 200}, {100, 200}};
    actions = state.tick(context, 2);
    const RuntimeGateAction* beginOpen = findAction(
        actions,
        RuntimeGateActionKind::BeginOpening,
        100
    );
    ok &= expect(beginOpen != nullptr, "allied request did not begin opening");
    ok &= expect(
        beginOpen != nullptr
            && beginOpen->soundKey == "GateUp"
            && beginOpen->soundId == "GateRise",
        "opening did not emit the GateUp sound intent"
    );
    const std::uint64_t onceHash = state.stateHash();
    actions = state.tick(context, 2);
    ok &= expect(actions.empty(), "duplicate tick emitted gate actions");
    ok &= expect(state.stateHash() == onceHash, "duplicate tick mutated state");

    world.requests.clear();
    actions = state.tick(context, 3);
    ok &= expect(actions.empty(), "opening completed too early");
    world.requests = {{100, 300}};
    actions = state.tick(context, 4);
    ok &= expect(
        findAction(actions, RuntimeGateActionKind::FullyOpen, 100) != nullptr,
        "gate did not become fully open"
    );
    ok &= expect(
        state.allowsPassage(context, 100, 300),
        "fully open gate did not allow an enemy ground mover"
    );
    ok &= expect(
        state.allowsPassage(context, 100, 200),
        "fully open gate did not allow an allied ground mover"
    );
    ok &= expect(
        !state.allowsPassage(context, 100, 301)
            && !state.allowsPassage(context, 100, 400),
        "open gate admitted a naval or aircraft mover"
    );

    // Footprint occupancy refreshes the idle timestamp.  Closing begins only
    // after the authored delay, and a newly occupied doorway reverses it.
    world.requests.clear();
    world.occupiedGateIds.insert(100);
    (void)state.tick(context, 5);
    (void)state.tick(context, 6);
    world.occupiedGateIds.clear();
    actions = state.tick(context, 7);
    ok &= expect(actions.empty(), "gate closed one tick into its idle delay");
    actions = state.tick(context, 8);
    ok &= expect(actions.empty(), "gate closed before its idle delay elapsed");
    actions = state.tick(context, 9);
    const RuntimeGateAction* beginClose = findAction(
        actions,
        RuntimeGateActionKind::BeginClosing,
        100
    );
    ok &= expect(beginClose != nullptr, "idle gate did not begin closing");
    ok &= expect(
        beginClose != nullptr
            && beginClose->soundKey == "GateDown"
            && beginClose->soundId == "GateLower",
        "closing did not emit the GateDown sound intent"
    );

    world.requests = {{100, 300}};
    actions = state.tick(context, 10);
    ok &= expect(
        actions.empty() && state.phaseOf(100) == RuntimeGatePhase::Closing,
        "enemy request reversed a closing gate"
    );
    world.requests.clear();
    world.occupiedGateIds.insert(100);
    actions = state.tick(context, 11);
    ok &= expect(
        findAction(actions, RuntimeGateActionKind::BeginOpening, 100)
            != nullptr,
        "occupied doorway was allowed to close"
    );
    world.occupiedGateIds.clear();
    (void)state.tick(context, 12);
    actions = state.tick(context, 13);
    ok &= expect(
        findAction(actions, RuntimeGateActionKind::FullyOpen, 100) != nullptr,
        "reversed gate did not return to fully open"
    );
    (void)state.tick(context, 14);
    (void)state.tick(context, 15);
    actions = state.tick(context, 16);
    ok &= expect(
        findAction(actions, RuntimeGateActionKind::BeginClosing, 100)
            != nullptr,
        "reopened gate did not close after becoming idle"
    );
    world.requests = {{100, 300}};
    (void)state.tick(context, 17);
    world.requests.clear();
    actions = state.tick(context, 18);
    ok &= expect(
        findAction(actions, RuntimeGateActionKind::FullyClosed, 100) != nullptr,
        "gate did not complete its close transition"
    );
    ok &= expect(
        !state.allowsPassage(context, 100, 300),
        "closed gate remained passable"
    );

    // A live type swap resets transition state; a non-gate type or dead host
    // is removed.  Restoring a new live gate creates a clean Closed state.
    world.requests = {{101, 200}};
    actions = state.tick(context, 19);
    ok &= expect(
        findAction(actions, RuntimeGateActionKind::BeginOpening, 101)
            != nullptr,
        "second gate did not begin opening"
    );
    WorldObjectInstance* gate101 = findMutable(world.objects, 101);
    ok &= expect(gate101 != nullptr, "test gate 101 disappeared");
    if (gate101 != nullptr) {
        gate101->type = "GATEA";
    }
    RuntimeGateReconcileReport report = state.reconcile(context);
    ok &= expect(
        report.resetGates == 1
            && state.phaseOf(101) == RuntimeGatePhase::Closed,
        "gate type change did not reset state"
    );
    if (gate101 != nullptr) {
        gate101->type = "NOTGATE";
    }
    WorldObjectInstance* gate100 = findMutable(world.objects, 100);
    if (gate100 != nullptr) {
        gate100->alive = false;
    }
    report = state.reconcile(context);
    ok &= expect(
        report.removedGates == 2 && state.gates().empty(),
        "dead/non-gate hosts retained gate state"
    );

    WorldObjectInstance gate102 = makeObject(
        102,
        WorldObjectKind::Structure,
        "Player",
        "GATEA"
    );
    world.objects.push_back(gate102);
    report = state.reconcile(context);
    ok &= expect(
        report.addedGates == 1
            && state.phaseOf(102) == RuntimeGatePhase::Closed,
        "new live gate was not restored as Closed"
    );

    // Save/load round-trip preserves every transition field and stable hash.
    world.objects.erase(
        std::remove_if(
            world.objects.begin(),
            world.objects.end(),
            [](const WorldObjectInstance& object) {
                return object.instanceId == 102;
            }
        ),
        world.objects.end()
    );
    gate100 = findMutable(world.objects, 100);
    gate101 = findMutable(world.objects, 101);
    if (gate100 != nullptr) {
        gate100->alive = true;
        gate100->health = 1000;
        gate100->type = "GATEA";
    }
    if (gate101 != nullptr) {
        gate101->alive = true;
        gate101->health = 1000;
        gate101->type = "GATEB";
    }
    world.requests = {{100, 200}};
    RuntimeGateSystemState saveSource({10, 2, 2});
    actions = saveSource.tick(context, 30);
    ok &= expect(
        findAction(actions, RuntimeGateActionKind::BeginOpening, 100)
            != nullptr,
        "save source did not enter an active transition"
    );
    std::ostringstream savedStream;
    saveSource.save(savedStream);
    const std::string saved = savedStream.str();
    ok &= expect(
        saved.find("GateId=100") < saved.find("GateId=101"),
        "serialized gates are not in stable instance-id order"
    );

    IniFile savedIni;
    ok &= expect(savedIni.loadFromString(saved), "saved gate INI did not parse");
    RuntimeGateSystemState restored;
    std::string error;
    report = {};
    ok &= expect(
        restored.load(savedIni, context, &error, &report),
        "valid gate save failed to load: " + error
    );
    ok &= expect(
        restored.stateHash() == saveSource.stateHash(),
        "save/load changed the stable gate hash"
    );
    const std::uint64_t restoredHash = restored.stateHash();
    ok &= expect(
        restored.tick(context, 30).empty()
            && restored.stateHash() == restoredHash,
        "restored duplicate tick was not idempotent"
    );

    // A syntactically valid but stale save is reconciled against the current
    // world: dead/type-changed hosts vanish and a new live gate is Closed.
    if (gate100 != nullptr) {
        gate100->alive = false;
    }
    if (gate101 != nullptr) {
        gate101->type = "NOTGATE";
    }
    world.objects.push_back(makeObject(
        102,
        WorldObjectKind::Structure,
        "Player",
        "GATEA"
    ));
    RuntimeGateSystemState dirtyRestored;
    report = {};
    ok &= expect(
        dirtyRestored.load(savedIni, context, &error, &report),
        "stale-world gate save failed to reconcile"
    );
    ok &= expect(
        report.removedGates == 2
            && report.addedGates == 1
            && dirtyRestored.gates().size() == 1
            && dirtyRestored.phaseOf(102) == RuntimeGatePhase::Closed,
        "stale gate save did not recover to the live world"
    );
    if (dirtyRestored.gates().size() == 1) {
        ok &= expect(
            dirtyRestored.gates()[0].gateId == 102,
            "dirty recovery retained the wrong gate id"
        );
    }

    // Strict schema and count limits reject oversized/malformed input without
    // mutating the destination state.
    std::string oversized = saved;
    ok &= expect(
        replaceOnce(oversized, "GateCount=2", "GateCount=5000"),
        "test could not patch saved gate count"
    );
    IniFile oversizedIni;
    ok &= expect(
        oversizedIni.loadFromString(oversized),
        "oversized gate save did not parse as INI"
    );
    const std::uint64_t beforeRejectedLoad = restored.stateHash();
    error.clear();
    ok &= expect(
        !restored.load(oversizedIni, context, &error, nullptr),
        "oversized gate save was accepted"
    );
    ok &= expect(
        !error.empty() && restored.stateHash() == beforeRejectedLoad,
        "rejected gate load mutated state or omitted an error"
    );

    if (!ok) {
        return 1;
    }
    std::cout << "gate system smoke passed\n";
    return 0;
}
