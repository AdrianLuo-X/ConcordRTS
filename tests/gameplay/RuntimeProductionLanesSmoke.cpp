#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"
#include "simulation/production/RuntimeProductionLanes.h"

#include <iostream>
#include <set>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "production lanes smoke failure: " << message << '\n';
    }
    return condition;
}

int totalDebited(
    const std::vector<RuntimeProductionAdvanceEvent>& events
) {
    int total = 0;
    for (const RuntimeProductionAdvanceEvent& event : events) {
        total += event.creditsDebited;
    }
    return total;
}

bool containsAdvanceKind(
    const std::vector<RuntimeProductionAdvanceEvent>& events,
    RuntimeProductionAdvanceKind kind,
    std::uint64_t serial = 0
) {
    for (const RuntimeProductionAdvanceEvent& event : events) {
        if (event.kind == kind
            && (serial == 0 || event.serial == serial)) {
            return true;
        }
    }
    return false;
}

} // namespace

#include "RuntimeNoCooldownProductionSmoke.inl"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: concord_production_lanes_smoke <rules>\n";
        return 2;
    }

    IniFile ini;
    bool success = expect(ini.loadFromFile(argv[1]), "fixture did not load");
    success &= smokeNoCooldownProduction();
    RulesDatabase rules;
    success &= expect(rules.loadFromIni(ini), "RulesDatabase load failed");
    success &= expect(
        rules.getMaximumQueuedObjects() == 7,
        "[General] MaximumQueuedObjects was not exposed"
    );

    IniFile defaultIni;
    success &= expect(
        defaultIni.loadFromString(
            "[InfantryTypes]\n[VehicleTypes]\n[AircraftTypes]\n"
            "[BuildingTypes]\n[OverlayTypes]\n[Tiberiums]\n"
            "[TerrainTypes]\n[Warheads]\n"
        ),
        "default rules text did not parse"
    );
    RulesDatabase defaultRules;
    success &= expect(
        defaultRules.loadFromIni(defaultIni)
            && defaultRules.getMaximumQueuedObjects() == 29,
        "MaximumQueuedObjects default is not 29"
    );

    for (const RuntimeProductionLane lane : runtimeProductionLanes) {
        const std::string_view name = runtimeProductionLaneName(lane);
        success &= expect(
            runtimeProductionLaneFromName(name) == lane,
            "lane name did not round-trip: " + std::string(name)
        );
    }
    success &= expect(
        runtimeProductionLaneFromName(" aircrafts ")
            == RuntimeProductionLane::Aircraft,
        "legacy Aircrafts name was not read compatibly"
    );
    success &= expect(
        !runtimeProductionLaneFromName("Unit").has_value(),
        "unknown lane name was silently accepted"
    );

    const BuildingType* power = rules.findBuildingType("GAPOWR");
    const BuildingType* wall = rules.findBuildingType("GAWALL");
    const BuildingType* constructionYard = rules.findBuildingType("GACNST");
    const BuildingType* barracks = rules.findBuildingType("GAPILE");
    const BuildingType* warFactory = rules.findBuildingType("GAWEAP");
    const BuildingType* shipyard = rules.findBuildingType("GAYARD");
    const BuildingType* airfield = rules.findBuildingType("GAAIRC");
    const UnitType* infantry = rules.findUnitType("InfantryType", "E1");
    const UnitType* tank = rules.findUnitType("UnitType", "MTNK");
    const UnitType* destroyer = rules.findUnitType("UnitType", "DEST");
    const UnitType* orca = rules.findUnitType("AircraftType", "ORCA");
    const UnitType* blackEagle =
        rules.findUnitType("AircraftType", "BEAG");
    const UnitType* zeppelin = rules.findUnitType("UnitType", "ZEP");
    const UnitType* disk = rules.findUnitType("UnitType", "DISK");

    success &= expect(
        power != nullptr && wall != nullptr && constructionYard != nullptr
            && barracks != nullptr && warFactory != nullptr
            && shipyard != nullptr && airfield != nullptr
            && infantry != nullptr && tank != nullptr && destroyer != nullptr
            && orca != nullptr && blackEagle != nullptr
            && zeppelin != nullptr && disk != nullptr,
        "fixture object lookup failed"
    );
    if (!success) {
        return 1;
    }

    success &= expect(
        runtimeProductionLaneForBuilding(*power)
            == RuntimeProductionLane::Structures,
        "non-Combat building did not map to Structures"
    );
    success &= expect(
        runtimeProductionLaneForBuilding(*wall)
            == RuntimeProductionLane::Armory,
        "GAWALL Combat building did not map to Armory"
    );
    success &= expect(
        runtimeProductionLaneForUnit(
            *infantry,
            RuntimeProductionUnitClass::Infantry
        ) == RuntimeProductionLane::Infantry,
        "E1 did not map to Infantry"
    );
    success &= expect(
        runtimeProductionLaneForUnit(
            *tank,
            RuntimeProductionUnitClass::Vehicle
        ) == RuntimeProductionLane::Vehicles,
        "MTNK did not map to Vehicles"
    );
    success &= expect(
        runtimeProductionLaneForUnit(
            *destroyer,
            RuntimeProductionUnitClass::Vehicle
        ) == RuntimeProductionLane::Ships,
        "DEST Naval=yes did not map to Ships"
    );
    UnitType waterBoundOnly;
    waterBoundOnly.id = "WATERBOUND_ONLY";
    waterBoundOnly.waterBound = true;
    success &= expect(
        runtimeProductionLaneForUnit(
            waterBoundOnly,
            RuntimeProductionUnitClass::Vehicle
        ) == RuntimeProductionLane::Ships,
        "WaterBound-only vehicle did not map to Ships"
    );
    success &= expect(
        runtimeProductionLaneForUnit(
            *orca,
            RuntimeProductionUnitClass::Aircraft
        ) == RuntimeProductionLane::Aircraft
            && runtimeProductionLaneForUnit(
                *blackEagle,
                RuntimeProductionUnitClass::Aircraft
            ) == RuntimeProductionLane::Aircraft,
        "ORCA/BEAG AircraftTypes did not map to Aircraft"
    );
    success &= expect(
        runtimeProductionLaneForUnit(
            *zeppelin,
            RuntimeProductionUnitClass::Vehicle
        ) == RuntimeProductionLane::Vehicles
            && runtimeProductionLaneForUnit(
                *disk,
                RuntimeProductionUnitClass::Vehicle
            ) == RuntimeProductionLane::Vehicles,
        "ZEP/DISK VehicleTypes changed lane because of ConsideredAircraft"
    );

    success &= expect(
        runtimeProductionFactorySupportsLane(
            *constructionYard,
            RuntimeProductionLane::Structures
        ) && runtimeProductionFactorySupportsLane(
            *constructionYard,
            RuntimeProductionLane::Armory
        ),
        "BuildingType factory did not service both building lanes"
    );
    success &= expect(
        runtimeProductionFactorySupportsLane(
            *barracks,
            RuntimeProductionLane::Infantry
        ),
        "InfantryType factory did not service Infantry"
    );
    success &= expect(
        runtimeProductionFactorySupportsLane(
            *warFactory,
            RuntimeProductionLane::Vehicles
        ) && !runtimeProductionFactorySupportsLane(
            *warFactory,
            RuntimeProductionLane::Ships
        ),
        "land UnitType factory crossed into the Ships lane"
    );
    success &= expect(
        runtimeProductionFactorySupportsLane(
            *shipyard,
            RuntimeProductionLane::Ships
        ) && !runtimeProductionFactorySupportsLane(
            *shipyard,
            RuntimeProductionLane::Vehicles
        ),
        "naval UnitType factory crossed into the Vehicles lane"
    );
    success &= expect(
        runtimeProductionFactorySupportsLane(
            *airfield,
            RuntimeProductionLane::Aircraft
        ),
        "AircraftType factory did not service Aircraft"
    );

    success &= expect(
        runtimeProductionLaneCapacity(
            RuntimeProductionLane::Structures,
            7,
            2
        ) == 1
            && runtimeProductionLaneCapacity(
                RuntimeProductionLane::Armory,
                7,
                2
            ) == 1
            && runtimeProductionLaneCapacity(
                RuntimeProductionLane::Infantry,
                7,
                2
            ) == 8
            && runtimeProductionLaneCapacity(
                RuntimeProductionLane::Aircraft,
                7,
                2
            ) == 2,
        "lane capacities did not follow original queue/dock rules"
    );

    RuntimeProductionLanesState lanes;
    auto queueInto = [](
        RuntimeProductionLanesState& target,
        RuntimeProductionLane lane,
        const std::string& item,
        int cost,
        int ticks,
        int maximumQueued = 7,
        int docks = 2,
        RuntimeProductionInsertMode mode = RuntimeProductionInsertMode::Back
    ) {
        RuntimeProductionLaneOrder order;
        order.itemKey = item;
        order.cost = cost;
        order.totalTicks = ticks;
        return runtimeQueueProductionOrder(
            target,
            lane,
            order,
            maximumQueued,
            docks,
            mode
        );
    };
    const auto queue = [&](RuntimeProductionLane lane,
                           const std::string& item,
                           int cost,
                           int ticks) {
        return queueInto(lanes, lane, item, cost, ticks);
    };
    const RuntimeProductionQueueResult structure = queue(
        RuntimeProductionLane::Structures, "GAPOWR", 8, 4
    );
    const RuntimeProductionQueueResult armory = queue(
        RuntimeProductionLane::Armory, "GAWALL", 4, 4
    );
    const RuntimeProductionQueueResult infantryOrder = queue(
        RuntimeProductionLane::Infantry, "E1", 4, 4
    );
    const RuntimeProductionQueueResult vehicleOrder = queue(
        RuntimeProductionLane::Vehicles, "MTNK", 8, 4
    );
    const RuntimeProductionQueueResult shipOrder = queue(
        RuntimeProductionLane::Ships, "DEST", 12, 4
    );
    const RuntimeProductionQueueResult aircraftOrder = queue(
        RuntimeProductionLane::Aircraft, "ORCA", 16, 4
    );
    success &= expect(
        structure.decision == RuntimeProductionQueueDecision::Accepted
            && armory.decision == RuntimeProductionQueueDecision::Accepted
            && infantryOrder.decision
                == RuntimeProductionQueueDecision::Accepted
            && vehicleOrder.decision
                == RuntimeProductionQueueDecision::Accepted
            && shipOrder.decision
                == RuntimeProductionQueueDecision::Accepted
            && aircraftOrder.decision
                == RuntimeProductionQueueDecision::Accepted,
        "valid orders were not accepted into all six independent lanes"
    );
    success &= expect(
        queue(RuntimeProductionLane::Structures, "GAREFN", 20, 4).decision
            == RuntimeProductionQueueDecision::LaneFull,
        "Structures accepted a second simultaneous item"
    );

    int credits = 100;
    std::set<std::string> completed;
    RuntimeProductionAdvanceHooks hooks;
    const std::vector<RuntimeProductionAdvanceEvent> firstTick =
        runtimeAdvanceProductionLanes(lanes, credits, hooks);
    int startedCount = 0;
    for (const RuntimeProductionAdvanceEvent& event : firstTick) {
        if (event.kind == RuntimeProductionAdvanceKind::Started) {
            ++startedCount;
        }
    }
    success &= expect(
        startedCount == 6
            && credits == 87
            && totalDebited(firstTick) == 13,
        "all six lanes did not start and charge one tick concurrently"
    );

    RuntimeProductionLanesState precisionProbe;
    queueInto(
        precisionProbe,
        RuntimeProductionLane::Infantry,
        "NON_DIVISIBLE",
        100,
        60
    );
    int precisionCredits = 100;
    int paidCompletionTick = 0;
    int currentPrecisionTick = 0;
    RuntimeProductionAdvanceHooks precisionHooks;
    currentPrecisionTick = 1;
    runtimeAdvanceProductionLanes(
        precisionProbe,
        precisionCredits,
        precisionHooks
    );
    const std::uint64_t precisionCarryHash =
        runtimeProductionLanesStateHash(precisionProbe);
    const std::string precisionCarrySerialized =
        runtimeSerializeProductionLanes(precisionProbe);
    RuntimeProductionLanesState restoredPrecisionProbe;
    std::string precisionLoadError;
    success &= expect(
        runtimeDeserializeProductionLanes(
            precisionCarrySerialized,
            restoredPrecisionProbe,
            &precisionLoadError
        ) && runtimeProductionLanesStateHash(restoredPrecisionProbe)
            == precisionCarryHash,
        "fractional rate carry did not survive schema 2 save/load: "
            + precisionLoadError
    );
    RuntimeProductionLanesState impossibleActive = precisionProbe;
    RuntimeProductionLaneState& impossibleActiveLane =
        impossibleActive.lanes[runtimeProductionLaneIndex(
            RuntimeProductionLane::Infantry
        )];
    if (impossibleActiveLane.active.has_value()) {
        --impossibleActiveLane.active->remainingTicks;
    }
    RuntimeProductionLanesState impossibleActiveDestination;
    success &= expect(
        !runtimeDeserializeProductionLanes(
            runtimeSerializeProductionLanes(impossibleActive),
            impossibleActiveDestination,
            &precisionLoadError
        ),
        "schema 2 accepted an active order with impossible remainingTicks"
    );
    precisionProbe = std::move(restoredPrecisionProbe);
    for (currentPrecisionTick = 2;
        currentPrecisionTick <= 60;
        ++currentPrecisionTick) {
        const std::vector<RuntimeProductionAdvanceEvent> events =
            runtimeAdvanceProductionLanes(
            precisionProbe,
            precisionCredits,
            precisionHooks
        );
        const std::optional<RuntimeProductionLaneOrder> completedOrder =
            runtimePeekCompletedProduction(
                precisionProbe,
                RuntimeProductionLane::Infantry
            );
        if (completedOrder.has_value()) {
            paidCompletionTick = currentPrecisionTick;
            success &= expect(
                containsAdvanceKind(
                    events,
                    RuntimeProductionAdvanceKind::CompletionDeferred,
                    completedOrder->serial
                ) && runtimeCommitCompletedProduction(
                    precisionProbe,
                    RuntimeProductionLane::Infantry,
                    completedOrder->serial
                ).has_value(),
                "paid completion was not explicitly committed"
            );
        }
    }
    success &= expect(
        paidCompletionTick == 60
            && precisionCredits == 0
            && runtimeProductionLaneOccupancy(
                precisionProbe,
                RuntimeProductionLane::Infantry
            ) == 0,
        "non-divisible progressive payment finished later than totalTicks"
    );

    RuntimeProductionLanesState freePrecisionProbe;
    queueInto(
        freePrecisionProbe,
        RuntimeProductionLane::Infantry,
        "FREE_NON_DIVISIBLE",
        0,
        3
    );
    int freeCredits = 0;
    int freeCompletionTick = 0;
    int currentFreeTick = 0;
    RuntimeProductionAdvanceHooks freePrecisionHooks;
    for (currentFreeTick = 1; currentFreeTick <= 3; ++currentFreeTick) {
        runtimeAdvanceProductionLanes(
            freePrecisionProbe,
            freeCredits,
            freePrecisionHooks
        );
        const std::optional<RuntimeProductionLaneOrder> completedOrder =
            runtimePeekCompletedProduction(
                freePrecisionProbe,
                RuntimeProductionLane::Infantry
            );
        if (completedOrder.has_value()) {
            freeCompletionTick = currentFreeTick;
            success &= expect(
                runtimeCommitCompletedProduction(
                    freePrecisionProbe,
                    RuntimeProductionLane::Infantry,
                    completedOrder->serial
                ).has_value(),
                "zero-cost completion was not explicitly committed"
            );
        }
    }
    success &= expect(
        freeCompletionTick == 3,
        "non-divisible zero-cost production finished later than totalTicks"
    );

    success &= expect(
        runtimeSetProductionHold(
            lanes,
            RuntimeProductionLane::Vehicles,
            true
        ) && runtimeSetProductionHold(
            lanes,
            RuntimeProductionLane::Vehicles,
            true
        ),
        "explicit vehicle lane Pause was not idempotent"
    );
    runtimeAdvanceProductionLanes(lanes, credits, hooks);
    const RuntimeProductionLaneState& heldVehicleLane =
        lanes.lanes[runtimeProductionLaneIndex(
            RuntimeProductionLane::Vehicles
        )];
    success &= expect(
        heldVehicleLane.active.has_value()
            && heldVehicleLane.active->creditsSpent == 2
            && heldVehicleLane.active->remainingTicks == 3,
        "held vehicle lane advanced while other lanes ran"
    );
    success &= expect(
        runtimeSetProductionHold(
            lanes,
            RuntimeProductionLane::Vehicles,
            false
        ) && runtimeSetProductionHold(
            lanes,
            RuntimeProductionLane::Vehicles,
            false
        ),
        "explicit vehicle lane Resume was not idempotent"
    );

    RuntimeProductionLanesState heldCancelProbe;
    queueInto(
        heldCancelProbe,
        RuntimeProductionLane::Vehicles,
        "CANCEL_HELD",
        1,
        1
    );
    runtimeSetProductionHold(
        heldCancelProbe,
        RuntimeProductionLane::Vehicles,
        true
    );
    const RuntimeProductionCancelResult heldCancel =
        runtimeCancelProductionOrder(
            heldCancelProbe,
            RuntimeProductionLane::Vehicles,
            "CANCEL_HELD"
        );
    const RuntimeProductionLaneState& heldCancelLane =
        heldCancelProbe.lanes[runtimeProductionLaneIndex(
            RuntimeProductionLane::Vehicles
        )];
    success &= expect(
        heldCancel.removed && !heldCancelLane.onHold,
        "canceling the last queued order left its lane held"
    );
    runtimeAdvanceProductionLanes(lanes, credits, hooks);
    runtimeAdvanceProductionLanes(lanes, credits, hooks);
    runtimeAdvanceProductionLanes(lanes, credits, hooks);
    const std::vector<RuntimeProductionCompletedOrder> firstCompletions =
        runtimeEnumerateCompletedProduction(lanes);
    bool firstCompletionsCommitted = firstCompletions.size() == 4U;
    std::uint64_t previousCompletionSerial = 0;
    for (const RuntimeProductionCompletedOrder& completion :
            firstCompletions) {
        firstCompletionsCommitted = firstCompletionsCommitted
            && completion.order.serial > previousCompletionSerial;
        previousCompletionSerial = completion.order.serial;
        completed.insert(completion.order.itemKey);
        firstCompletionsCommitted = firstCompletionsCommitted
            && runtimeCommitCompletedProduction(
                lanes,
                completion.lane,
                completion.order.serial
            ).has_value();
    }
    success &= expect(
        firstCompletionsCommitted,
        "completed orders were not enumerated and committed by serial"
    );
    success &= expect(
        lanes.lanes[runtimeProductionLaneIndex(
            RuntimeProductionLane::Structures
        )].ready.has_value()
            && lanes.lanes[runtimeProductionLaneIndex(
                RuntimeProductionLane::Armory
            )].ready.has_value(),
        "Structures and Armory did not retain independent ready items"
    );
    success &= expect(
        completed.contains("E1")
            && completed.contains("DEST")
            && completed.contains("ORCA")
            && completed.contains("MTNK"),
        "unit lanes did not complete independently"
    );

    RuntimeProductionLanesState paymentProbe;
    queueInto(
        paymentProbe,
        RuntimeProductionLane::Infantry,
        "E1",
        10,
        4
    );
    queueInto(
        paymentProbe,
        RuntimeProductionLane::Infantry,
        "E1",
        10,
        4
    );
    int poorCredits = 1;
    const std::vector<RuntimeProductionAdvanceEvent> partialPaymentEvents =
        runtimeAdvanceProductionLanes(paymentProbe, poorCredits);
    const RuntimeProductionLaneState& paymentLane =
        paymentProbe.lanes[runtimeProductionLaneIndex(
            RuntimeProductionLane::Infantry
        )];
    success &= expect(
        paymentLane.active.has_value()
            && paymentLane.active->creditsSpent == 1
            && poorCredits == 0
            && totalDebited(partialPaymentEvents) == 1,
        "production did not charge only the affordable partial amount"
    );
    const std::vector<RuntimeProductionAdvanceEvent> zeroCreditEvents =
        runtimeAdvanceProductionLanes(paymentProbe, poorCredits);
    success &= expect(
        paymentLane.active.has_value()
            && paymentLane.active->creditsSpent == 1
            && totalDebited(zeroCreditEvents) == 0,
        "zero-credit production advanced or forgot partial spending"
    );
    const RuntimeProductionCancelResult queuedCancel =
        runtimeCancelProductionOrder(
            paymentProbe,
            RuntimeProductionLane::Infantry,
            "E1"
        );
    success &= expect(
        queuedCancel.removed
            && queuedCancel.refund == 0
            && paymentLane.active.has_value()
            && paymentLane.active->creditsSpent == 1,
        "canceling a repeated item removed the paid active copy before tail"
    );
    const RuntimeProductionCancelResult partialCancel =
        runtimeCancelProductionOrder(
            paymentProbe,
            RuntimeProductionLane::Infantry,
            "E1"
        );
    success &= expect(
        partialCancel.removed && partialCancel.refund == 1,
        "partial production did not refund exactly creditsSpent"
    );

    RuntimeProductionLanesState exactCancelProbe;
    const RuntimeProductionQueueResult exactActive = queueInto(
        exactCancelProbe,
        RuntimeProductionLane::Infantry,
        "REPEATED",
        8,
        4
    );
    const RuntimeProductionQueueResult exactQueued = queueInto(
        exactCancelProbe,
        RuntimeProductionLane::Infantry,
        "REPEATED",
        8,
        4
    );
    int exactCancelCredits = 2;
    runtimeAdvanceProductionLanes(exactCancelProbe, exactCancelCredits);
    const RuntimeProductionCancelResult exactPaidCancel =
        runtimeCancelProductionSerial(
            exactCancelProbe,
            RuntimeProductionLane::Infantry,
            exactActive.serial
        );
    const RuntimeProductionLaneState& exactCancelLane =
        exactCancelProbe.lanes[runtimeProductionLaneIndex(
            RuntimeProductionLane::Infantry
        )];
    success &= expect(
        exactActive.decision == RuntimeProductionQueueDecision::Accepted
            && exactQueued.decision
                == RuntimeProductionQueueDecision::Accepted
            && exactPaidCancel.removed
            && exactPaidCancel.order.serial == exactActive.serial
            && exactPaidCancel.refund == 2
            && !exactCancelLane.active.has_value()
            && exactCancelLane.queued.size() == 1U
            && exactCancelLane.queued.front().serial == exactQueued.serial
            && !runtimeCancelProductionSerial(
                exactCancelProbe,
                RuntimeProductionLane::Infantry,
                exactActive.serial
            ).removed,
        "serial cancel did not target the paid copy of a repeated item"
    );

    RuntimeProductionLanesState addNextProbe;
    queueInto(
        addNextProbe,
        RuntimeProductionLane::Vehicles,
        "FIRST",
        100,
        100
    );
    int oneCredit = 1;
    runtimeAdvanceProductionLanes(addNextProbe, oneCredit);
    queueInto(
        addNextProbe,
        RuntimeProductionLane::Vehicles,
        "TAIL",
        1,
        1
    );
    queueInto(
        addNextProbe,
        RuntimeProductionLane::Vehicles,
        "URGENT",
        1,
        1,
        7,
        2,
        RuntimeProductionInsertMode::AfterActive
    );
    success &= expect(
        addNextProbe.lanes[runtimeProductionLaneIndex(
            RuntimeProductionLane::Vehicles
        )].queued.front().itemKey == "URGENT",
        "AddNext did not insert immediately after the active item"
    );

    RuntimeProductionLanesState historicalSerialProbe;
    RuntimeProductionLaneOrder historicalFirst;
    historicalFirst.itemKey = "HISTORICAL_FIRST";
    historicalFirst.cost = 1;
    historicalFirst.totalTicks = 1;
    const RuntimeProductionQueueResult historicalAccepted =
        runtimeQueueProductionOrder(
            historicalSerialProbe,
            RuntimeProductionLane::Vehicles,
            historicalFirst,
            7,
            7
        );
    const RuntimeProductionCancelResult historicalRemoved =
        runtimeCancelProductionSerial(
            historicalSerialProbe,
            RuntimeProductionLane::Vehicles,
            historicalAccepted.serial
        );
    const std::uint64_t historicalHashBefore =
        runtimeProductionLanesStateHash(historicalSerialProbe);
    const std::uint64_t historicalNextBefore =
        historicalSerialProbe.nextSerial;
    RuntimeProductionLaneOrder staleSerial = historicalFirst;
    staleSerial.itemKey = "STALE_REUSE";
    staleSerial.serial = historicalAccepted.serial;
    const RuntimeProductionQueueResult staleReuse =
        runtimeQueueProductionOrder(
            historicalSerialProbe,
            RuntimeProductionLane::Ships,
            staleSerial,
            7,
            7
        );
    success &= expect(
        historicalAccepted.decision
                == RuntimeProductionQueueDecision::Accepted
            && historicalRemoved.removed
            && staleReuse.decision
                != RuntimeProductionQueueDecision::Accepted
            && historicalSerialProbe.nextSerial == historicalNextBefore
            && runtimeProductionLanesStateHash(historicalSerialProbe)
                == historicalHashBefore,
        "retired global production serial was reused or rejection mutated state"
    );

    RuntimeProductionLanesState balanceProbe;
    queueInto(
        balanceProbe,
        RuntimeProductionLane::Ships,
        "SHIP",
        1,
        1
    );
    queueInto(
        balanceProbe,
        RuntimeProductionLane::Aircraft,
        "AIR",
        1,
        1
    );
    int lastCredit = 1;
    std::vector<std::string> balanceCompleted;
    RuntimeProductionAdvanceHooks balanceHooks;
    const std::vector<RuntimeProductionAdvanceEvent> balanceEvents =
        runtimeAdvanceProductionLanes(
            balanceProbe,
            lastCredit,
            balanceHooks
        );
    for (const RuntimeProductionCompletedOrder& completion :
            runtimeEnumerateCompletedProduction(balanceProbe)) {
        balanceCompleted.push_back(completion.order.itemKey);
        success &= expect(
            runtimeCommitCompletedProduction(
                balanceProbe,
                completion.lane,
                completion.order.serial
            ).has_value(),
            "last-credit completion did not commit"
        );
    }
    const RuntimeProductionLaneState& waitingAircraft =
        balanceProbe.lanes[runtimeProductionLaneIndex(
            RuntimeProductionLane::Aircraft
        )];
    success &= expect(
        balanceCompleted.size() == 1
            && balanceCompleted.front() == "SHIP"
            && totalDebited(balanceEvents) == 1
            && waitingAircraft.active.has_value()
            && waitingAircraft.active->creditsSpent == 0,
        "Ships did not win the deterministic last-credit tie over Aircraft"
    );

    RuntimeProductionLanesState lowPowerProbe;
    const RuntimeProductionQueueResult lowPowerOrder = queueInto(
        lowPowerProbe,
        RuntimeProductionLane::Vehicles,
        "LOW_POWER",
        10,
        3
    );
    RuntimeProductionAdvanceHooks lowPowerHooks;
    lowPowerHooks.productionRate = [](
        RuntimeProductionLane,
        const RuntimeProductionLaneOrder&
    ) {
        return RuntimeProductionRate{1, 2};
    };
    int lowPowerCredits = 10;
    int lowPowerDebit = 0;
    const std::vector<RuntimeProductionAdvanceEvent> lowPowerFirst =
        runtimeAdvanceProductionLanes(
            lowPowerProbe,
            lowPowerCredits,
            lowPowerHooks
        );
    lowPowerDebit += totalDebited(lowPowerFirst);
    success &= expect(
        lowPowerOrder.decision == RuntimeProductionQueueDecision::Accepted
            && lowPowerDebit > 0
            && lowPowerDebit < 10
            && runtimeSetProductionHold(
                lowPowerProbe,
                RuntimeProductionLane::Vehicles,
                true
            ),
        "low-power order did not enter a partial active state"
    );
    const std::vector<RuntimeProductionAdvanceEvent> lowPowerHeld =
        runtimeAdvanceProductionLanes(
            lowPowerProbe,
            lowPowerCredits,
            lowPowerHooks
        );
    const std::uint64_t heldLowPowerHash =
        runtimeProductionLanesStateHash(lowPowerProbe);
    RuntimeProductionLanesState restoredLowPower;
    std::string lowPowerLoadError;
    success &= expect(
        totalDebited(lowPowerHeld) == 0
            && runtimeDeserializeProductionLanes(
                runtimeSerializeProductionLanes(lowPowerProbe),
                restoredLowPower,
                &lowPowerLoadError
            )
            && runtimeProductionLanesStateHash(restoredLowPower)
                == heldLowPowerHash
            && runtimeSetProductionHold(
                restoredLowPower,
                RuntimeProductionLane::Vehicles,
                false
            ),
        "held low-power fractional state did not round-trip: "
            + lowPowerLoadError
    );
    lowPowerProbe = std::move(restoredLowPower);
    for (int tick = 0;
        tick < 12
            && !runtimePeekCompletedProduction(
                lowPowerProbe,
                RuntimeProductionLane::Vehicles
            ).has_value();
        ++tick) {
        lowPowerDebit += totalDebited(runtimeAdvanceProductionLanes(
            lowPowerProbe,
            lowPowerCredits,
            lowPowerHooks
        ));
    }
    const std::optional<RuntimeProductionLaneOrder> lowPowerCompleted =
        runtimePeekCompletedProduction(
            lowPowerProbe,
            RuntimeProductionLane::Vehicles
        );
    success &= expect(
        lowPowerCompleted.has_value()
            && lowPowerDebit == 10
            && lowPowerCredits == 0
            && runtimeCommitCompletedProduction(
                lowPowerProbe,
                RuntimeProductionLane::Vehicles,
                lowPowerCompleted->serial
            ).has_value(),
        "low-power debit did not aggregate exactly to authored cost"
    );

    RuntimeProductionLanesState deferredProbe;
    const RuntimeProductionQueueResult deferredOrder = queueInto(
        deferredProbe,
        RuntimeProductionLane::Vehicles,
        "BLOCKED_EXIT",
        6,
        3
    );
    int deferredCredits = 6;
    int deferredDebit = 0;
    std::vector<RuntimeProductionAdvanceEvent> deferredEvents;
    for (int tick = 0; tick < 3; ++tick) {
        deferredEvents = runtimeAdvanceProductionLanes(
            deferredProbe,
            deferredCredits
        );
        deferredDebit += totalDebited(deferredEvents);
    }
    const std::optional<RuntimeProductionLaneOrder> deferredCompleted =
        runtimePeekCompletedProduction(
            deferredProbe,
            RuntimeProductionLane::Vehicles
        );
    const std::uint64_t deferredHash =
        runtimeProductionLanesStateHash(deferredProbe);
    RuntimeProductionLanesState restoredDeferred;
    std::string deferredLoadError;
    success &= expect(
        deferredOrder.decision == RuntimeProductionQueueDecision::Accepted
            && deferredCompleted.has_value()
            && deferredDebit == 6,
        "blocked completion setup failed"
    );
    success &= expect(
        containsAdvanceKind(
            deferredEvents,
            RuntimeProductionAdvanceKind::CompletionDeferred,
            deferredOrder.serial
        ) && runtimeDeserializeProductionLanes(
            runtimeSerializeProductionLanes(deferredProbe),
            restoredDeferred,
            &deferredLoadError
        ) && runtimeProductionLanesStateHash(restoredDeferred)
            == deferredHash,
        "paid-complete deferred state did not round-trip: "
            + deferredLoadError
    );
    deferredProbe = std::move(restoredDeferred);
    const int creditsBeforeDeferredWait = deferredCredits;
    bool stayedDeferred = true;
    for (int tick = 0; tick < 3; ++tick) {
        const std::vector<RuntimeProductionAdvanceEvent> events =
            runtimeAdvanceProductionLanes(
                deferredProbe,
                deferredCredits
            );
        stayedDeferred = stayedDeferred
            && totalDebited(events) == 0
            && containsAdvanceKind(
                events,
                RuntimeProductionAdvanceKind::CompletionDeferred,
                deferredOrder.serial
            )
            && runtimePeekCompletedProduction(
                deferredProbe,
                RuntimeProductionLane::Vehicles
            ).has_value();
    }
    const std::vector<RuntimeProductionCompletedOrder> deferredEnumeration =
        runtimeEnumerateCompletedProduction(deferredProbe);
    RuntimeProductionLanesState impossibleCompletedHold = deferredProbe;
    impossibleCompletedHold.lanes[runtimeProductionLaneIndex(
        RuntimeProductionLane::Vehicles
    )].onHold = true;
    RuntimeProductionLanesState completedHoldVictim = deferredProbe;
    const std::uint64_t completedHoldVictimHash =
        runtimeProductionLanesStateHash(completedHoldVictim);
    std::string completedHoldError;
    success &= expect(
        !runtimeDeserializeProductionLanes(
            runtimeSerializeProductionLanes(impossibleCompletedHold),
            completedHoldVictim,
            &completedHoldError
        ) && runtimeProductionLanesStateHash(completedHoldVictim)
            == completedHoldVictimHash,
        "save accepted an impossible held completed order"
    );
    success &= expect(
        stayedDeferred
            && deferredCredits == creditsBeforeDeferredWait
            && deferredEnumeration.size() == 1U
            && deferredEnumeration.front().order.serial
                == deferredOrder.serial
            && !runtimeCommitCompletedProduction(
                deferredProbe,
                RuntimeProductionLane::Vehicles,
                deferredOrder.serial + 1
            ).has_value()
            && runtimeCommitCompletedProduction(
                deferredProbe,
                RuntimeProductionLane::Vehicles,
                deferredOrder.serial
            ).has_value()
            && !runtimeCommitCompletedProduction(
                deferredProbe,
                RuntimeProductionLane::Vehicles,
                deferredOrder.serial
            ).has_value(),
        "blocked completion was lost, double-debited, or double-committed"
    );

    RuntimeProductionLanesState aircraftCapacityProbe;
    success &= expect(
        queueInto(
            aircraftCapacityProbe,
            RuntimeProductionLane::Aircraft,
            "ORCA",
            10,
            2,
            1,
            3
        ).decision == RuntimeProductionQueueDecision::Accepted
            && queueInto(
                aircraftCapacityProbe,
                RuntimeProductionLane::Aircraft,
                "ORCA",
                10,
                2,
                1,
                3
            ).decision == RuntimeProductionQueueDecision::Accepted
            && queueInto(
                aircraftCapacityProbe,
                RuntimeProductionLane::Aircraft,
                "ORCA",
                10,
                2,
                1,
                3
            ).decision == RuntimeProductionQueueDecision::LaneFull
            && queueInto(
                aircraftCapacityProbe,
                RuntimeProductionLane::Aircraft,
                "HARRIER",
                10,
                2,
                1,
                3
            ).decision == RuntimeProductionQueueDecision::Accepted,
        "Aircraft per-item or total dock capacity was not enforced"
    );
    const RuntimeProductionReconcileResult shrunk =
        runtimeReconcileProductionLaneCapacity(
            aircraftCapacityProbe,
            RuntimeProductionLane::Aircraft,
            1,
            1
        );
    success &= expect(
        runtimeProductionLaneOccupancy(
            aircraftCapacityProbe,
            RuntimeProductionLane::Aircraft
        ) == 1
            && shrunk.removed.size() == 2,
        "Aircraft queue did not reconcile after dock capacity shrank"
    );

    const RuntimeProductionLane invalidLane =
        static_cast<RuntimeProductionLane>(255);
    RuntimeProductionLaneOrder invalidOrder;
    invalidOrder.itemKey = "BAD";
    success &= expect(
        runtimeProductionLaneIndex(invalidLane)
                == runtimeProductionLanes.size()
            && runtimeProductionLaneCapacity(invalidLane, 7, 2) == 0
            && runtimeQueueProductionOrder(
                lanes,
                invalidLane,
                invalidOrder,
                7,
                2
            ).decision == RuntimeProductionQueueDecision::InvalidOrder
            && !runtimeSetProductionHold(lanes, invalidLane, true)
            && !runtimeCancelProductionOrder(
                lanes,
                invalidLane,
                "GAPOWR"
            ).removed
            && !runtimeConsumeReadyProduction(
                lanes,
                invalidLane,
                1
            ).has_value(),
        "invalid lane silently aliased the Structures lane"
    );

    const std::uint64_t savedHash = runtimeProductionLanesStateHash(lanes);
    const std::string serialized = runtimeSerializeProductionLanes(lanes);
    RuntimeProductionLanesState restored;
    std::string loadError;
    success &= expect(
        runtimeDeserializeProductionLanes(serialized, restored, &loadError),
        "production state did not deserialize: " + loadError
    );
    success &= expect(
        runtimeProductionLanesStateHash(restored) == savedHash
            && runtimeSerializeProductionLanes(restored) == serialized,
        "production save/load/hash round-trip was not deterministic"
    );
    RuntimeProductionLanesState hashChanged = restored;
    RuntimeProductionLaneState& hashStructure =
        hashChanged.lanes[runtimeProductionLaneIndex(
            RuntimeProductionLane::Structures
        )];
    if (hashStructure.ready.has_value()) {
        ++hashStructure.ready->creditRemainderMicros;
    }
    success &= expect(
        runtimeProductionLanesStateHash(hashChanged) != savedHash,
        "production hash omitted fractional payment state"
    );
    RuntimeProductionLanesState unchanged = restored;
    const std::uint64_t unchangedHash =
        runtimeProductionLanesStateHash(unchanged);
    std::string malformed = serialized;
    malformed.replace(5, 1, "9");
    success &= expect(
        !runtimeDeserializeProductionLanes(malformed, unchanged, &loadError)
            && runtimeProductionLanesStateHash(unchanged) == unchangedHash,
        "malformed production save partially mutated live state"
    );
    RuntimeProductionLanesState invalidReadyState = restored;
    RuntimeProductionLaneState& invalidReadyLane =
        invalidReadyState.lanes[runtimeProductionLaneIndex(
            RuntimeProductionLane::Structures
        )];
    if (invalidReadyLane.ready.has_value()) {
        invalidReadyLane.ready->rateDivisionRemainder = 1;
        invalidReadyLane.ready->rateDivisionDenominator = 1;
    }
    const std::string invalidReady =
        runtimeSerializeProductionLanes(invalidReadyState);
    success &= expect(
        !runtimeDeserializeProductionLanes(invalidReady, unchanged, &loadError)
            && runtimeProductionLanesStateHash(unchanged) == unchangedHash,
        "schema 2 accepted unreachable fractional state on a ready order"
    );

    const RuntimeProductionLaneState& restoredStructures =
        restored.lanes[runtimeProductionLaneIndex(
            RuntimeProductionLane::Structures
        )];
    const RuntimeProductionLaneState& restoredArmory =
        restored.lanes[runtimeProductionLaneIndex(
            RuntimeProductionLane::Armory
        )];
    const std::uint64_t structureReadySerial =
        restoredStructures.ready.has_value()
            ? restoredStructures.ready->serial
            : 0;
    const std::uint64_t armoryReadySerial =
        restoredArmory.ready.has_value()
            ? restoredArmory.ready->serial
            : 0;
    const std::uint64_t beforeWrongReadyConsumeHash =
        runtimeProductionLanesStateHash(restored);
    const bool wrongReadyRejected =
        !runtimeConsumeReadyProduction(
            restored,
            RuntimeProductionLane::Structures,
            armoryReadySerial
        ).has_value()
        && runtimeProductionLanesStateHash(restored)
            == beforeWrongReadyConsumeHash;
    const std::optional<RuntimeProductionLaneOrder> ready =
        runtimeConsumeReadyProduction(
            restored,
            RuntimeProductionLane::Structures,
            structureReadySerial
        );
    const bool armorySurvivedStructureConsume =
        restored.lanes[runtimeProductionLaneIndex(
            RuntimeProductionLane::Armory
        )].ready.has_value()
        && restored.lanes[runtimeProductionLaneIndex(
            RuntimeProductionLane::Armory
        )].ready->serial == armoryReadySerial;
    const std::optional<RuntimeProductionLaneOrder> armoryReady =
        runtimeConsumeReadyProduction(
            restored,
            RuntimeProductionLane::Armory,
            armoryReadySerial
        );
    success &= expect(
        wrongReadyRejected
            && ready.has_value()
            && ready->itemKey == "GAPOWR"
            && ready->creditsSpent == ready->cost
            && ready->remainingTicks == 0
            && armorySurvivedStructureConsume
            && armoryReady.has_value()
            && armoryReady->itemKey == "GAWALL",
        "independent placement-ready lanes were not consumed by exact serial"
    );

    if (success) {
        std::cout << "production lanes smoke passed: queueMax="
                  << rules.getMaximumQueuedObjects()
                  << " mappings=GAWALL/E1/MTNK/DEST/ORCA/BEAG/ZEP/DISK"
                  << " concurrent=6 saveHash=" << savedHash << '\n';
    }
    return success ? 0 : 1;
}
