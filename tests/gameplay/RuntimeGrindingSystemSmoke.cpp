#include "simulation/buildings/RuntimeGrindingSystem.h"

#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "grinding smoke failure: " << message << '\n';
    }
    return condition;
}

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            if (character >= 'A' && character <= 'Z') {
                return static_cast<char>(character - 'A' + 'a');
            }
            return static_cast<char>(character);
        }
    );
    return value;
}

WorldObjectInstance makeStructure(
    int id,
    const std::string& owner,
    const std::string& type,
    bool powered = true
) {
    WorldObjectInstance object;
    object.instanceId = id;
    object.kind = WorldObjectKind::Structure;
    object.owner = owner;
    object.type = type;
    object.health = 1000;
    object.x = 10;
    object.y = 10;
    object.alive = true;
    object.poweredOperational = powered;
    return object;
}

WorldObjectInstance makeTechno(
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
    object.health = 100;
    object.x = id % 17;
    object.y = id % 13;
    object.alive = true;
    object.poweredOperational = true;
    return object;
}

WorldObjectInstance* findMutable(
    std::vector<WorldObjectInstance>& objects,
    int objectId
) {
    const auto found = std::find_if(
        objects.begin(),
        objects.end(),
        [objectId](const WorldObjectInstance& object) {
            return object.instanceId == objectId;
        }
    );
    return found == objects.end() ? nullptr : &*found;
}

struct SmokeWorld {
    const RulesDatabase* rules = nullptr;
    std::vector<WorldObjectInstance> objects;
    std::set<int> activeSources;
    std::set<int> insideSources;

    int purchaseValue(const WorldObjectInstance& object) const {
        if (rules == nullptr) {
            return 0;
        }
        const UnitType* type = nullptr;
        if (object.kind == WorldObjectKind::Infantry) {
            type = rules->findUnitType("InfantryType", object.type);
        } else if (object.kind == WorldObjectKind::Unit) {
            type = rules->findUnitType("UnitType", object.type);
        }
        return type == nullptr ? 0 : std::max(0, type->cost);
    }

    RuntimeGrindingContext context() {
        RuntimeGrindingContext result;
        result.rules = rules;
        result.objects = &objects;
        result.canonicalOwnerId = [](const std::string& owner) {
            return lowerAscii(owner);
        };
        result.areAllied = [](const std::string& left,
                              const std::string& right) {
            const std::string leftKey = lowerAscii(left);
            const std::string rightKey = lowerAscii(right);
            return (leftKey == "yuri" && rightKey == "ally")
                || (leftKey == "ally" && rightKey == "yuri")
                || leftKey == rightKey;
        };
        result.isEntryActive = [this](int, int sourceId) {
            return activeSources.find(sourceId) != activeSources.end();
        };
        result.isInsideHostFootprint = [this](int, int sourceId) {
            return insideSources.find(sourceId) != insideSources.end();
        };
        result.purchaseValue = [this](const WorldObjectInstance& object) {
            return purchaseValue(object);
        };
        result.currentValue = [this](const WorldObjectInstance& object) {
            return std::max(0, purchaseValue(object) - 137);
        };
        return result;
    }
};

const RuntimeGrindingAction* findRemoval(
    const std::vector<RuntimeGrindingAction>& actions,
    int objectId
) {
    const auto found = std::find_if(
        actions.begin(),
        actions.end(),
        [objectId](const RuntimeGrindingAction& action) {
            return action.kind == RuntimeGrindingActionKind::RemoveUnit
                && action.objectId == objectId;
        }
    );
    return found == actions.end() ? nullptr : &*found;
}

bool expectVeto(
    SmokeWorld& world,
    int hostId,
    int sourceId,
    RuntimeGrindingVeto expected,
    const std::string& label
) {
    RuntimeGrindingSystemState state;
    const RuntimeGrindingCommandResult result = state.queueEntry(
        world.context(),
        hostId,
        sourceId,
        1
    );
    return expect(!result.accepted && result.veto == expected, label);
}

bool enterAndRemove(
    SmokeWorld& world,
    int hostId,
    int sourceId,
    int tick,
    std::vector<RuntimeGrindingAction>& actions
) {
    RuntimeGrindingSystemState state;
    const RuntimeGrindingCommandResult queued = state.queueEntry(
        world.context(),
        hostId,
        sourceId,
        tick
    );
    if (!expect(queued.accepted, "valid source was not queued")) {
        return false;
    }
    world.activeSources.insert(sourceId);
    const RuntimeGrindingCommandResult entering = state.beginEnter(
        world.context(),
        hostId,
        sourceId,
        tick + 1
    );
    if (!expect(entering.accepted, "queued source did not begin entry")) {
        return false;
    }
    if (!expect(
            state.tick(world.context(), tick + 1).empty(),
            "source was removed before reaching the footprint"
        )) {
        return false;
    }
    world.insideSources.insert(sourceId);
    actions = state.tick(world.context(), tick + 2);
    const bool oneShot = state.tick(world.context(), tick + 2).empty()
        && state.tick(world.context(), tick + 3).empty()
        && state.relations().empty();
    world.activeSources.erase(sourceId);
    world.insideSources.erase(sourceId);
    return expect(oneShot, "grinding removal was not idempotent");
}

bool replaceOnce(
    std::string& text,
    const std::string& before,
    const std::string& after
) {
    const std::size_t position = text.find(before);
    if (position == std::string::npos) {
        return false;
    }
    text.replace(position, before.size(), after);
    return true;
}

bool runStockProbe(const std::string& path) {
    IniFile ini;
    RulesDatabase rules;
    if (!expect(
            ini.loadFromFile(path) && rules.loadFromIni(ini),
            "stock rules did not load"
        )) {
        return false;
    }
    const BuildingType* grinder = rules.findBuildingType("YAGRND");
    const UnitType* gi = rules.findUnitType("InfantryType", "E1");
    const UnitType* engineer = rules.findUnitType(
        "InfantryType",
        "ENGINEER"
    );
    return expect(
        std::abs(rules.getRefundPercent() - 0.5) < 0.000001
            && grinder != nullptr
            && grinder->grinding
            && gi != nullptr
            && gi->soylent == 100
            && engineer != nullptr
            && engineer->soylent == 250,
        "stock YAGRND/Soylent/RefundPercent rules diverged"
    );
}

bool runFixtureProbe(const std::string& path) {
    IniFile ini;
    RulesDatabase rules;
    if (!expect(
            ini.loadFromFile(path) && rules.loadFromIni(ini),
            "fixture rules did not load"
        )) {
        return false;
    }

    const UnitType* gi = rules.findUnitType("InfantryType", "GI");
    const UnitType* fallback = rules.findUnitType(
        "InfantryType",
        "NOSOYLENT"
    );
    const UnitType* tank = rules.findUnitType("UnitType", "TANK");
    const BuildingType* grinder = rules.findBuildingType("YAGRND");
    if (!expect(
            std::abs(rules.getRefundPercent() - 0.5) < 0.000001
                && gi != nullptr
                && gi->soylent == 100
                && fallback != nullptr
                && fallback->soylent == 0
                && tank != nullptr
                && tank->soylent == 700
                && grinder != nullptr
                && grinder->grinding
                && grinder->soylent == 77,
            "fixture Soylent/RefundPercent parse diverged"
        )) {
        return false;
    }

    IniFile decimalRatioIni;
    RulesDatabase decimalRatioRules;
    if (!expect(
            decimalRatioIni.loadFromString(
                "[General]\nRefundPercent=.5\n"
            )
                && decimalRatioRules.loadFromIni(decimalRatioIni)
                && std::abs(
                    decimalRatioRules.getRefundPercent() - 0.5
                ) < 0.000001,
            "decimal RefundPercent was not parsed as a strict ratio"
        )) {
        return false;
    }

    IniFile invalidRatioIni;
    RulesDatabase invalidRatioRules;
    if (!expect(
            invalidRatioIni.loadFromString(
                "[General]\nRefundPercent=50\n"
            )
                && invalidRatioRules.loadFromIni(invalidRatioIni)
                && std::abs(
                    invalidRatioRules.getRefundPercent() - 0.5
                ) < 0.000001
                && !invalidRatioRules.getWarnings().empty(),
            "bare percentage was not rejected by strict ratio parsing"
        )) {
        return false;
    }

    SmokeWorld world;
    world.rules = &rules;
    world.objects = {
        makeStructure(100, "Yuri", "YAGRND"),
        makeStructure(101, "Yuri", "NONGRIND"),
        makeStructure(102, "Yuri", "YAGRND", false),
        makeTechno(200, WorldObjectKind::Infantry, "Yuri", "GI"),
        makeTechno(201, WorldObjectKind::Infantry, "Yuri", "ENGINEER"),
        makeTechno(202, WorldObjectKind::Infantry, "Yuri", "NOSOYLENT"),
        makeTechno(203, WorldObjectKind::Infantry, "Yuri", "ZEROVALUE"),
        makeTechno(210, WorldObjectKind::Unit, "Yuri", "TANK"),
        makeTechno(211, WorldObjectKind::Unit, "Yuri", "CHRONO"),
        makeTechno(212, WorldObjectKind::Unit, "Yuri", "FLYER"),
        makeTechno(213, WorldObjectKind::Unit, "Yuri", "NAVAL"),
        makeTechno(214, WorldObjectKind::Unit, "Yuri", "MINDTANK"),
        makeTechno(215, WorldObjectKind::Infantry, "Ally", "GI"),
        makeTechno(216, WorldObjectKind::Infantry, "Enemy", "GI"),
        makeTechno(220, WorldObjectKind::Unit, "Yuri", "TRANSPORT"),
        makeTechno(221, WorldObjectKind::Infantry, "Yuri", "GI"),
        makeTechno(222, WorldObjectKind::Unit, "Yuri", "TANK"),
        makeTechno(223, WorldObjectKind::Unit, "Yuri", "TRANSPORT"),
        makeTechno(225, WorldObjectKind::Infantry, "Yuri", "GI"),
        makeTechno(226, WorldObjectKind::Infantry, "Yuri", "GI")
    };
    findMutable(world.objects, 220)->transportPassengerIds = {222, 221};
    for (const int passengerId : {221, 222}) {
        WorldObjectInstance* passenger = findMutable(
            world.objects,
            passengerId
        );
        passenger->alive = false;
        passenger->mission = "InsideTransport";
    }
    findMutable(world.objects, 225)->garrisonHostObjectId = 101;
    findMutable(world.objects, 226)->alive = false;
    findMutable(world.objects, 226)->mission = "Guard";

    std::vector<RuntimeGrindingAction> actions;
    if (!enterAndRemove(world, 100, 200, 10, actions)) {
        return false;
    }
    const RuntimeGrindingAction* giRemoval = findRemoval(actions, 200);
    if (!expect(
            actions.size() == 1U
                && giRemoval != nullptr
                && giRemoval->removalRole
                    == RuntimeGrindingRemovalRole::Root
                && giRemoval->terminalDisposition
                    == RuntimeGrindingTerminalDisposition::ConsumeInsideGrinder
                && giRemoval->refundCredits == 100,
            "Soylent override removal contract/refund diverged"
        )) {
        return false;
    }

    if (!enterAndRemove(world, 100, 202, 20, actions)) {
        return false;
    }
    const RuntimeGrindingAction* fallbackRemoval = findRemoval(actions, 202);
    if (!expect(
            actions.size() == 1U
                && fallbackRemoval != nullptr
                && fallbackRemoval->purchaseValue == 301
                && fallbackRemoval->currentValue == 164
                && fallbackRemoval->refundCredits == 150,
            "fallback refund was not floor(cost * RefundPercent)"
        )) {
        return false;
    }

    RuntimeGrindingSystemState ownershipState;
    const RuntimeGrindingCommandResult mindControlled =
        ownershipState.queueEntry(world.context(), 100, 214, 30);
    if (!expect(
            mindControlled.accepted,
            "live mind-controlled owner was replaced by authored owner"
        )) {
        return false;
    }
    ownershipState.cancelSource(214, "probe cleanup");

    RuntimeGrindingSystemState vehicleState;
    if (!expect(
            vehicleState.queueEntry(world.context(), 100, 210, 31).accepted,
            "eligible ground vehicle was rejected"
        )) {
        return false;
    }
    vehicleState.cancelSource(210, "probe cleanup");

    if (!expectVeto(
            world,
            100,
            201,
            RuntimeGrindingVeto::SourceEngineer,
            "engineer was accepted"
        )
        || !expectVeto(
            world,
            100,
            211,
            RuntimeGrindingVeto::SourceChrono,
            "chrono locomotor was accepted"
        )
        || !expectVeto(
            world,
            100,
            212,
            RuntimeGrindingVeto::SourceFlying,
            "flying source was accepted"
        )
        || !expectVeto(
            world,
            100,
            213,
            RuntimeGrindingVeto::SourceNaval,
            "naval source was accepted"
        )
        || !expectVeto(
            world,
            100,
            215,
            RuntimeGrindingVeto::SourceAlliedNotOwned,
            "allied source was accepted"
        )
        || !expectVeto(
            world,
            100,
            216,
            RuntimeGrindingVeto::SourceEnemy,
            "enemy source was accepted"
        )
        || !expectVeto(
            world,
            100,
            203,
            RuntimeGrindingVeto::SourceHasNoRefund,
            "zero-refund source was accepted"
        )
        || !expectVeto(
            world,
            102,
            200,
            RuntimeGrindingVeto::HostUnpowered,
            "unpowered grinder was accepted"
        )
        || !expectVeto(
            world,
            101,
            200,
            RuntimeGrindingVeto::HostNotGrinder,
            "non-grinder host was accepted"
        )
        || !expectVeto(
            world,
            100,
            221,
            RuntimeGrindingVeto::SourceTransported,
            "boarded passenger was accepted as a root source"
        )
        || !expectVeto(
            world,
            100,
            225,
            RuntimeGrindingVeto::SourceGarrisoned,
            "garrisoned source was accepted"
        )
        || !expectVeto(
            world,
            100,
            226,
            RuntimeGrindingVeto::SourceUnavailable,
            "ordinary inactive object was accepted as retained passenger"
        )) {
        return false;
    }

    WorldObjectInstance* rootTransport = findMutable(world.objects, 220);
    rootTransport->alive = false;
    RuntimeGrindingSystemState inactiveRootState;
    const RuntimeGrindingCommandResult inactiveRoot =
        inactiveRootState.queueEntry(world.context(), 100, 220, 39);
    rootTransport->alive = true;
    if (!expect(
            !inactiveRoot.accepted
                && inactiveRoot.veto
                    == RuntimeGrindingVeto::SourceUnavailable
                && inactiveRoot.actions.empty()
                && inactiveRootState.relations().empty(),
            "inactive root transport was not rejected atomically"
        )) {
        return false;
    }

    rootTransport->transportPassengerIds.push_back(999);
    RuntimeGrindingSystemState wrongManifestState;
    const RuntimeGrindingCommandResult wrongManifest =
        wrongManifestState.queueEntry(world.context(), 100, 220, 39);
    rootTransport->transportPassengerIds.pop_back();
    if (!expect(
            !wrongManifest.accepted
                && wrongManifest.veto
                    == RuntimeGrindingVeto::PassengerManifestInvalid
                && wrongManifest.actions.empty()
                && wrongManifestState.relations().empty(),
            "wrong passenger manifest was not rejected atomically"
        )) {
        return false;
    }

    WorldObjectInstance* secondTransport = findMutable(world.objects, 223);
    secondTransport->transportPassengerIds = {221};
    RuntimeGrindingSystemState secondContainerState;
    const RuntimeGrindingCommandResult secondContainer =
        secondContainerState.queueEntry(world.context(), 100, 220, 39);
    secondTransport->transportPassengerIds.clear();
    if (!expect(
            !secondContainer.accepted
                && secondContainer.veto
                    == RuntimeGrindingVeto::PassengerManifestInvalid
                && secondContainer.actions.empty()
                && secondContainerState.relations().empty(),
            "passenger in a second live container was not rejected atomically"
        )) {
        return false;
    }

    if (!enterAndRemove(world, 100, 220, 40, actions)) {
        return false;
    }
    const RuntimeGrindingAction* passengerGi = findRemoval(actions, 221);
    const RuntimeGrindingAction* passengerTank = findRemoval(actions, 222);
    const RuntimeGrindingAction* transport = findRemoval(actions, 220);
    const int totalRefund = std::accumulate(
        actions.begin(),
        actions.end(),
        0,
        [](int total, const RuntimeGrindingAction& action) {
            return total + action.refundCredits;
        }
    );
    if (!expect(
            actions.size() == 3U
                && actions[0].objectId == 221
                && actions[1].objectId == 222
                && actions[2].objectId == 220
                && passengerGi != nullptr
                && passengerGi->removalRole
                    == RuntimeGrindingRemovalRole::Passenger
                && passengerGi->terminalDisposition
                    == RuntimeGrindingTerminalDisposition::ConsumeInsideGrinder
                && passengerGi->batchIndex == 0
                && passengerGi->batchSize == 3
                && passengerGi->refundCredits == 100
                && passengerTank != nullptr
                && passengerTank->removalRole
                    == RuntimeGrindingRemovalRole::Passenger
                && passengerTank->terminalDisposition
                    == RuntimeGrindingTerminalDisposition::ConsumeInsideGrinder
                && passengerTank->batchIndex == 1
                && passengerTank->refundCredits == 700
                && transport != nullptr
                && transport->removalRole
                    == RuntimeGrindingRemovalRole::Root
                && transport->terminalDisposition
                    == RuntimeGrindingTerminalDisposition::ConsumeInsideGrinder
                && transport->batchIndex == 2
                && transport->refundCredits == 600
                && totalRefund == 1400,
            "transport/passenger atomic snapshot refund diverged"
        )) {
        return false;
    }

    RuntimeGrindingSystemState staged;
    if (!expect(
            staged.queueEntry(world.context(), 100, 214, 50).accepted,
            "roundtrip source did not queue"
        )) {
        return false;
    }
    world.activeSources.insert(214);
    const std::uint64_t queuedHash = staged.stateHash();
    if (!expect(
            staged.beginEnter(world.context(), 100, 214, 51).accepted,
            "roundtrip source did not enter"
        )) {
        return false;
    }
    if (!expect(
            staged.stateHash() != queuedHash,
            "queued/entering phase did not influence hash"
        )) {
        return false;
    }
    std::ostringstream saved;
    staged.save(saved);
    IniFile savedIni;
    if (!expect(savedIni.loadFromString(saved.str()), "saved state did not parse")) {
        return false;
    }
    RuntimeGrindingSystemState restored;
    std::string loadError;
    RuntimeGrindingReconcileReport loadReport;
    if (!expect(
            restored.load(
                savedIni,
                world.context(),
                &loadError,
                &loadReport
            )
                && loadError.empty()
                && loadReport.retainedRelations == 1U
                && restored.stateHash() == staged.stateHash(),
            "mid-entry roundtrip diverged: " + loadError
        )) {
        return false;
    }
    RuntimeGrindingSystemState hashProbe = restored;
    hashProbe.cancelSource(214, "hash probe");
    if (!expect(
            hashProbe.stateHash() != restored.stateHash(),
            "relation phase/state did not influence hash"
        )) {
        return false;
    }

    std::string corruptText = saved.str();
    if (!expect(
            replaceOnce(corruptText, "HostId=100", "HostId=999"),
            "could not construct corrupt save"
        )) {
        return false;
    }
    IniFile corruptIni;
    if (!expect(corruptIni.loadFromString(corruptText), "corrupt save did not parse")) {
        return false;
    }
    const std::uint64_t beforeBadLoad = restored.stateHash();
    loadError.clear();
    if (!expect(
            !restored.load(corruptIni, world.context(), &loadError)
                && !loadError.empty()
                && restored.stateHash() == beforeBadLoad,
            "bad save was not rejected atomically"
        )) {
        return false;
    }

    RuntimeGrindingSystemState staleProbe = restored;
    WorldObjectInstance* liveHost = findMutable(world.objects, 100);
    liveHost->poweredOperational = false;
    const RuntimeGrindingReconcileReport staleReport =
        staleProbe.reconcile(world.context());
    liveHost->poweredOperational = true;
    if (!expect(
            staleReport.contextValid
                && staleReport.releasedRelations == 1U
                && staleProbe.relations().empty()
                && staleReport.actions.size() == 1U
                && staleReport.actions.front().kind
                    == RuntimeGrindingActionKind::Release,
            "stale grinder relation was not reconciled"
        )) {
        return false;
    }

    IniFile legacyIni;
    if (!expect(
            legacyIni.loadFromString("[OtherRuntime]\nVersion=1\n"),
            "legacy save did not parse"
        )) {
        return false;
    }
    RuntimeGrindingSystemState legacyProbe = restored;
    if (!expect(
            legacyProbe.load(legacyIni, world.context(), &loadError)
                && legacyProbe.relations().empty()
                && legacyProbe.lastAdvancedTick() == -1,
            "legacy save without GrindingRuntime did not reset safely"
        )) {
        return false;
    }

    world.activeSources.erase(214);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--stock") {
        return runStockProbe(argv[2]) ? 0 : 1;
    }
    if (argc != 2) {
        std::cerr << "usage: concord_grinding_system_smoke <rules.ini>\n"
                  << "   or: concord_grinding_system_smoke --stock <rulesmd.ini>\n";
        return 2;
    }
    return runFixtureProbe(argv[1]) ? 0 : 1;
}
