#pragma once

#include "simulation/production/RuntimeProductionLanes.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

class IniFile;

// A blocked building gift is a delivery obligation, not a second build order.
struct RuntimePendingFreeUnit {
    int hostId = -1;
    std::string owner;
    std::string buildingType;
    std::string unitType;
};

struct RuntimeFactoryDeliveryCell {
    int x = -1;
    int y = -1;
    bool onBridge = false;

    bool operator==(const RuntimeFactoryDeliveryCell&) const = default;
};

enum class RuntimeFactoryDeliveryDomain : std::uint8_t {
    Land,
    Naval
};

struct RuntimeFactoryDeliveryLease {
    std::string canonicalOwner;
    int hostId = -1;
    // Land factories use the synthetic door claim -1. Naval factories use
    // their single production foundation claim 0.
    int dockIndex = -1;
    RuntimeProductionLane lane = RuntimeProductionLane::Vehicles;
    std::uint64_t orderSerial = 0;
    int producedObjectId = -1;
    int acquiredTick = 0;
    RuntimeFactoryDeliveryCell rallyCell;
    RuntimeFactoryDeliveryDomain domain =
        RuntimeFactoryDeliveryDomain::Land;
};

enum class RuntimeFactoryDeliveryAcquireVeto : std::uint8_t {
    None,
    InvalidRequest,
    CapacityFull,
    RepairDockClaimed,
    FactoryDockClaimed,
    OrderAlreadyLeased,
    ObjectAlreadyLeased
};

struct RuntimeFactoryDeliveryAcquireRequest {
    RuntimeFactoryDeliveryLease lease;
    // Supplied by the host after consulting RuntimeRepairDepotSystem. This
    // module deliberately has no direct dependency on repair state.
    bool repairDockClaimed = false;
};

struct RuntimeFactoryDeliveryAcquireResult {
    bool acquired = false;
    RuntimeFactoryDeliveryAcquireVeto veto =
        RuntimeFactoryDeliveryAcquireVeto::None;
    RuntimeFactoryDeliveryLease lease;
    std::string reason;
};

enum class RuntimeFactoryDeliveryReleaseReason : std::uint8_t {
    ExplicitObject,
    ExplicitHost,
    ExplicitOrder,
    HostUnavailable,
    HostMismatch,
    ObjectUnavailable,
    ObjectMismatch,
    OrderUnavailable,
    OrderMismatch,
    BlockedOrderNotRetained
};

struct RuntimeFactoryDeliveryRelease {
    RuntimeFactoryDeliveryLease lease;
    RuntimeFactoryDeliveryReleaseReason reason =
        RuntimeFactoryDeliveryReleaseReason::ObjectUnavailable;
};

struct RuntimeFactoryDeliveryHostSnapshot {
    std::string canonicalOwner;
    int hostId = -1;
    RuntimeFactoryDeliveryDomain domain =
        RuntimeFactoryDeliveryDomain::Land;
    bool available = true;
    // A completed order may acquire a lease immediately before spawn. If
    // spawn remains blocked and no object exists yet, only a host explicitly
    // advertising this contract keeps that lease during reconciliation.
    bool retainBlockedOrder = false;
};

struct RuntimeFactoryDeliveryObjectSnapshot {
    std::string canonicalOwner;
    int objectId = -1;
    bool available = true;
};

struct RuntimeFactoryDeliveryOrderSnapshot {
    std::string canonicalOwner;
    RuntimeProductionLane lane = RuntimeProductionLane::Vehicles;
    std::uint64_t orderSerial = 0;
    bool blocked = false;
};

struct RuntimeFactoryDeliveryReconcileInput {
    std::vector<RuntimeFactoryDeliveryHostSnapshot> hosts;
    std::vector<RuntimeFactoryDeliveryObjectSnapshot> objects;
    std::vector<RuntimeFactoryDeliveryOrderSnapshot> orders;
};

struct RuntimeFactoryDeliveryReconcileReport {
    bool accepted = false;
    std::size_t retainedLeases = 0;
    std::size_t releasedLeases = 0;
    std::vector<RuntimeFactoryDeliveryRelease> releases;
    std::string error;
};

class RuntimeFactoryDeliveryState {
public:
    void reset();

    RuntimeFactoryDeliveryAcquireResult acquire(
        const RuntimeFactoryDeliveryAcquireRequest& request
    );

    std::vector<RuntimeFactoryDeliveryRelease> releaseByObject(
        int producedObjectId
    );
    std::vector<RuntimeFactoryDeliveryRelease> releaseByHost(int hostId);
    std::vector<RuntimeFactoryDeliveryRelease> releaseByOrder(
        std::string_view canonicalOwner,
        RuntimeProductionLane lane,
        std::uint64_t orderSerial
    );

    bool claimsDock(int hostId, int dockIndex) const;

    RuntimeFactoryDeliveryReconcileReport reconcile(
        const RuntimeFactoryDeliveryReconcileInput& input
    );

    void save(std::ostream& output) const;
    bool load(
        const IniFile& ini,
        std::string* errorMessage = nullptr
    );
    std::uint64_t stateHash() const;

    const std::vector<RuntimeFactoryDeliveryLease>& leases() const;
    bool deferFreeUnit(RuntimePendingFreeUnit grant);
    void completeFreeUnit(int hostId);
    const std::vector<RuntimePendingFreeUnit>& pendingFreeUnits() const;

private:
    std::vector<RuntimeFactoryDeliveryLease> leases_;
    std::vector<RuntimePendingFreeUnit> pendingFreeUnits_;

    void sortState();
};

const char* runtimeFactoryDeliveryDomainName(
    RuntimeFactoryDeliveryDomain domain
);
const char* runtimeFactoryDeliveryAcquireVetoName(
    RuntimeFactoryDeliveryAcquireVeto veto
);
const char* runtimeFactoryDeliveryReleaseReasonName(
    RuntimeFactoryDeliveryReleaseReason reason
);

