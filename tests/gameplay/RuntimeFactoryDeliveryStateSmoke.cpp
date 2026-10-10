#include "simulation/production/RuntimeFactoryDeliveryState.h"

#include "core/io/IniFile.h"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "factory delivery smoke failure: " << message << '\n';
    }
    return condition;
}

RuntimeFactoryDeliveryLease landLease(
    const std::string& owner,
    int hostId,
    std::uint64_t orderSerial,
    int objectId,
    int tick = 7
) {
    RuntimeFactoryDeliveryLease lease;
    lease.canonicalOwner = owner;
    lease.hostId = hostId;
    lease.dockIndex = -1;
    lease.lane = RuntimeProductionLane::Vehicles;
    lease.orderSerial = orderSerial;
    lease.producedObjectId = objectId;
    lease.acquiredTick = tick;
    lease.rallyCell = {hostId + 1, hostId + 2, true};
    lease.domain = RuntimeFactoryDeliveryDomain::Land;
    return lease;
}

RuntimeFactoryDeliveryLease navalLease(
    const std::string& owner,
    int hostId,
    std::uint64_t orderSerial,
    int objectId,
    int tick = 7
) {
    RuntimeFactoryDeliveryLease lease;
    lease.canonicalOwner = owner;
    lease.hostId = hostId;
    lease.dockIndex = 0;
    lease.lane = RuntimeProductionLane::Ships;
    lease.orderSerial = orderSerial;
    lease.producedObjectId = objectId;
    lease.acquiredTick = tick;
    lease.rallyCell = {hostId + 1, hostId + 2, false};
    lease.domain = RuntimeFactoryDeliveryDomain::Naval;
    return lease;
}

RuntimeFactoryDeliveryAcquireResult acquire(
    RuntimeFactoryDeliveryState& state,
    const RuntimeFactoryDeliveryLease& lease,
    bool repairDockClaimed = false
) {
    RuntimeFactoryDeliveryAcquireRequest request;
    request.lease = lease;
    request.repairDockClaimed = repairDockClaimed;
    return state.acquire(request);
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

std::string saveText(const RuntimeFactoryDeliveryState& state) {
    std::ostringstream output;
    state.save(output);
    return output.str();
}

bool runSmoke() {
    bool success = true;

    {
        RuntimeFactoryDeliveryState state;
        const std::uint64_t emptyHash = state.stateHash();
        const RuntimeFactoryDeliveryAcquireResult repairClaimed = acquire(
            state,
            landLease("Alpha", 100, 1, 1000),
            true
        );
        success &= expect(
            !repairClaimed.acquired
                && repairClaimed.veto
                    == RuntimeFactoryDeliveryAcquireVeto::RepairDockClaimed
                && state.leases().empty()
                && state.stateHash() == emptyHash,
            "repair dock claim veto is atomic"
        );

        RuntimeFactoryDeliveryLease invalid = landLease(
            "Alpha", 100, 1, 1000
        );
        invalid.dockIndex = 0;
        success &= expect(
            !acquire(state, invalid).acquired
                && state.stateHash() == emptyHash,
            "land lease rejects a naval dock index"
        );
        invalid = navalLease("Alpha", 100, 1, 1000);
        invalid.lane = RuntimeProductionLane::Vehicles;
        success &= expect(
            !acquire(state, invalid).acquired
                && state.stateHash() == emptyHash,
            "naval lease rejects a land lane"
        );
    }

    {
        RuntimeFactoryDeliveryState state;
        const RuntimeFactoryDeliveryAcquireResult first = acquire(
            state,
            landLease("Alpha", 100, 10, 1000, 20)
        );
        const std::uint64_t firstHash = state.stateHash();
        const RuntimeFactoryDeliveryAcquireResult sameDock = acquire(
            state,
            landLease("Alpha", 100, 11, 1001, 20)
        );
        const RuntimeFactoryDeliveryAcquireResult sameOrder = acquire(
            state,
            landLease("Alpha", 101, 10, 1001, 20)
        );
        const RuntimeFactoryDeliveryAcquireResult sameObject = acquire(
            state,
            landLease("Beta", 102, 10, 1000, 20)
        );
        success &= expect(
            first.acquired
                && sameDock.veto
                    == RuntimeFactoryDeliveryAcquireVeto::FactoryDockClaimed
                && sameOrder.veto
                    == RuntimeFactoryDeliveryAcquireVeto::OrderAlreadyLeased
                && sameObject.veto
                    == RuntimeFactoryDeliveryAcquireVeto::ObjectAlreadyLeased
                && state.stateHash() == firstHash
                && state.claimsDock(100, -1),
            "same-tick dock/order/object uniqueness is atomic"
        );

        const RuntimeFactoryDeliveryAcquireResult crossOwnerSameOrder =
            acquire(
                state,
                landLease("Beta", 101, 10, 1001, 20)
            );
        success &= expect(
            crossOwnerSameOrder.acquired
                && state.leases().size() == 2,
            "different houses may use the same per-house order serial"
        );

        const RuntimeFactoryDeliveryAcquireResult second = acquire(
            state,
            navalLease("Beta", 200, 20, 2000, 20)
        );
        const std::uint64_t twoOwnerHash = state.stateHash();
        const std::vector<RuntimeFactoryDeliveryRelease> wrongOwner =
            state.releaseByOrder(
                "Alpha",
                RuntimeProductionLane::Ships,
                20
            );
        success &= expect(
            second.acquired
                && state.leases().size() == 3
                && state.leases()[0].canonicalOwner == "Alpha"
                && state.leases()[1].canonicalOwner == "Beta"
                && state.leases()[2].canonicalOwner == "Beta"
                && state.claimsDock(200, 0)
                && wrongOwner.empty()
                && state.stateHash() == twoOwnerHash,
            "canonical owners remain isolated"
        );
    }

    {
        RuntimeFactoryDeliveryState state;
        success &= expect(
            acquire(
                state,
                landLease("Alpha", 310, 30, 3100)
            ).acquired
                && acquire(
                    state,
                    landLease("Beta", 320, 30, 3200)
                ).acquired,
            "same-serial cross-house reconcile setup"
        );
        RuntimeFactoryDeliveryReconcileInput input;
        input.hosts = {
            {"Alpha", 310, RuntimeFactoryDeliveryDomain::Land, true, true},
            {"Beta", 320, RuntimeFactoryDeliveryDomain::Land, true, true}
        };
        input.orders = {
            {"Alpha", RuntimeProductionLane::Vehicles, 30, true},
            {"Beta", RuntimeProductionLane::Vehicles, 30, true}
        };
        const RuntimeFactoryDeliveryReconcileReport report =
            state.reconcile(input);
        success &= expect(
            report.accepted
                && report.retainedLeases == 2
                && report.releasedLeases == 0
                && state.leases().size() == 2,
            "reconcile keys production orders by house and serial"
        );
    }

    {
        RuntimeFactoryDeliveryState state;
        success &= expect(
            acquire(
                state,
                landLease("Alpha", 300, 30, 3000)
            ).acquired,
            "pre-spawn blocked-order lease setup"
        );
        RuntimeFactoryDeliveryReconcileInput input;
        input.hosts.push_back({
            "Alpha",
            300,
            RuntimeFactoryDeliveryDomain::Land,
            true,
            true
        });
        input.orders.push_back({
            "Alpha",
            RuntimeProductionLane::Vehicles,
            30,
            true
        });
        RuntimeFactoryDeliveryReconcileReport report = state.reconcile(input);
        success &= expect(
            report.accepted
                && report.retainedLeases == 1
                && report.releasedLeases == 0
                && state.leases().size() == 1,
            "host contract retains a blocked completed order before spawn"
        );
        input.hosts.front().retainBlockedOrder = false;
        report = state.reconcile(input);
        success &= expect(
            report.accepted
                && report.retainedLeases == 0
                && report.releasedLeases == 1
                && report.releases.size() == 1
                && report.releases.front().reason
                    == RuntimeFactoryDeliveryReleaseReason::
                        BlockedOrderNotRetained
                && state.leases().empty(),
            "blocked order requires an explicit host retention contract"
        );
    }

    {
        RuntimeFactoryDeliveryState state;
        success &= expect(
            acquire(
                state,
                navalLease("Beta", 400, 40, 4000)
            ).acquired,
            "naval live-object reconcile setup"
        );
        RuntimeFactoryDeliveryReconcileInput input;
        input.hosts.push_back({
            "Beta",
            400,
            RuntimeFactoryDeliveryDomain::Naval,
            true,
            false
        });
        input.objects.push_back({"Beta", 4000, true});
        const RuntimeFactoryDeliveryReconcileReport report =
            state.reconcile(input);
        success &= expect(
            report.accepted
                && report.retainedLeases == 1
                && report.releasedLeases == 0
                && state.claimsDock(400, 0),
            "live naval delivery remains claimed without position guessing"
        );
        const std::vector<RuntimeFactoryDeliveryRelease> explicitRelease =
            state.releaseByObject(4000);
        success &= expect(
            explicitRelease.size() == 1
                && explicitRelease.front().reason
                    == RuntimeFactoryDeliveryReleaseReason::ExplicitObject
                && explicitRelease.front().lease.domain
                    == RuntimeFactoryDeliveryDomain::Naval
                && state.leases().empty()
                && !state.claimsDock(400, 0),
            "naval host explicitly releases a ship after foundation exit"
        );
    }

    {
        RuntimeFactoryDeliveryState state;
        acquire(state, landLease("Beta", 600, 60, 6000));
        acquire(state, landLease("Alpha", 500, 50, 5000));
        RuntimeFactoryDeliveryReconcileInput input;
        input.hosts = {
            {"Beta", 600, RuntimeFactoryDeliveryDomain::Land, false, false},
            {"Alpha", 500, RuntimeFactoryDeliveryDomain::Land, false, false}
        };
        const RuntimeFactoryDeliveryReconcileReport report =
            state.reconcile(input);
        success &= expect(
            report.accepted
                && report.releasedLeases == 2
                && report.releases.size() == 2
                && report.releases[0].lease.canonicalOwner == "Alpha"
                && report.releases[0].lease.hostId == 500
                && report.releases[1].lease.canonicalOwner == "Beta"
                && report.releases[1].lease.hostId == 600
                && state.leases().empty(),
            "reconcile release report has stable owner/host order"
        );
    }

    {
        RuntimeFactoryDeliveryState state;
        acquire(state, landLease("Alpha", 700, 70, 7000));
        acquire(state, landLease("Alpha", 701, 71, 7001));
        acquire(state, navalLease("Beta", 800, 80, 8000));
        const std::vector<RuntimeFactoryDeliveryRelease> byOrder =
            state.releaseByOrder(
                "Alpha",
                RuntimeProductionLane::Vehicles,
                71
            );
        const std::vector<RuntimeFactoryDeliveryRelease> byHost =
            state.releaseByHost(700);
        const std::vector<RuntimeFactoryDeliveryRelease> byObject =
            state.releaseByObject(8000);
        success &= expect(
            byOrder.size() == 1
                && byOrder.front().reason
                    == RuntimeFactoryDeliveryReleaseReason::ExplicitOrder
                && byHost.size() == 1
                && byHost.front().reason
                    == RuntimeFactoryDeliveryReleaseReason::ExplicitHost
                && byObject.size() == 1
                && byObject.front().reason
                    == RuntimeFactoryDeliveryReleaseReason::ExplicitObject
                && state.leases().empty(),
            "explicit object/host/order release APIs"
        );
    }

    {
        RuntimeFactoryDeliveryState source;
        success &= expect(
            acquire(
                source,
                landLease("Beta", 102, 102, 1002, 13)
            ).acquired
                && acquire(
                    source,
                    landLease("Alpha", 101, 101, 1001, 12)
                ).acquired,
            "roundtrip source setup"
        );
        const std::string saved = saveText(source);
        const std::uint64_t savedHash = source.stateHash();
        IniFile savedIni;
        RuntimeFactoryDeliveryState restored;
        std::string loadError;
        success &= expect(
            savedIni.loadFromString(saved)
                && restored.load(savedIni, &loadError)
                && loadError.empty()
                && restored.stateHash() == savedHash
                && saveText(restored) == saved
                && restored.leases().size() == 2
                && restored.leases()[0].canonicalOwner == "Alpha"
                && restored.leases()[0].rallyCell.onBridge,
            "mid-delivery schema-1 roundtrip and hash"
        );

        RuntimeFactoryDeliveryState victim;
        success &= expect(
            victim.load(savedIni, &loadError),
            "bad-load victim baseline"
        );
        const std::uint64_t victimHash = victim.stateHash();

        const auto rejectMutation = [&](std::string corrupted,
                                        const std::string& message) {
            IniFile corruptedIni;
            loadError.clear();
            const bool parsed = corruptedIni.loadFromString(corrupted);
            return expect(
                parsed
                    && !victim.load(corruptedIni, &loadError)
                    && !loadError.empty()
                    && victim.stateHash() == victimHash,
                message
            );
        };

        std::string duplicateDock = saved;
        replaceFirst(duplicateDock, "HostId=102", "HostId=101");
        success &= rejectMutation(
            duplicateDock,
            "duplicate host/dock save is rejected atomically"
        );
        std::string duplicateOrder = saved;
        replaceFirst(
            duplicateOrder,
            "OrderSerial=102",
            "OrderSerial=101"
        );
        replaceFirst(
            duplicateOrder,
            "CanonicalOwnerHex=42657461",
            "CanonicalOwnerHex=416c706861"
        );
        success &= rejectMutation(
            duplicateOrder,
            "duplicate same-house order save is rejected atomically"
        );
        std::string duplicateObject = saved;
        replaceFirst(
            duplicateObject,
            "ProducedObjectId=1002",
            "ProducedObjectId=1001"
        );
        success &= rejectMutation(
            duplicateObject,
            "duplicate produced object save is rejected atomically"
        );
        std::string badDomain = saved;
        replaceFirst(badDomain, "Domain=Land", "Domain=Space");
        success &= rejectMutation(
            badDomain,
            "invalid domain save is rejected atomically"
        );

        IniFile oversizedIni;
        oversizedIni.loadFromString(
            "[FactoryDeliveryRuntime]\n"
            "Version=1\n"
            "LeaseCount=4097\n"
        );
        loadError.clear();
        success &= expect(
            !victim.load(oversizedIni, &loadError)
                && !loadError.empty()
                && victim.stateHash() == victimHash,
            "schema cap is rejected atomically"
        );

        RuntimeFactoryDeliveryReconcileInput duplicateSnapshots;
        duplicateSnapshots.hosts = {
            {"Alpha", 101, RuntimeFactoryDeliveryDomain::Land, true, false},
            {"Alpha", 101, RuntimeFactoryDeliveryDomain::Land, true, false}
        };
        const RuntimeFactoryDeliveryReconcileReport badReport =
            victim.reconcile(duplicateSnapshots);
        success &= expect(
            !badReport.accepted
                && !badReport.error.empty()
                && victim.stateHash() == victimHash,
            "duplicate reconcile snapshots leave state unchanged"
        );

        IniFile absent;
        absent.loadFromString("[OtherRuntime]\nVersion=1\n");
        success &= expect(
            restored.load(absent, &loadError)
                && restored.leases().empty()
                && loadError.empty(),
            "save without factory delivery section resets to empty"
        );
    }

    {
        RuntimeFactoryDeliveryState bounded;
        bool allAccepted = true;
        for (int index = 0; index < 4096; ++index) {
            allAccepted = acquire(
                bounded,
                landLease(
                    "Capacity",
                    10000 + index,
                    static_cast<std::uint64_t>(100000 + index),
                    200000 + index,
                    99
                )
            ).acquired && allAccepted;
        }
        const RuntimeFactoryDeliveryAcquireResult overflow = acquire(
            bounded,
            landLease("Capacity", 20000, 200000, 300000, 99)
        );
        success &= expect(
            allAccepted
                && bounded.leases().size() == 4096
                && !overflow.acquired
                && overflow.veto
                    == RuntimeFactoryDeliveryAcquireVeto::CapacityFull,
            "runtime lease cap is deterministic"
        );
    }

    return success;
}

} // namespace

int main() {
    return runSmoke() ? 0 : 1;
}
