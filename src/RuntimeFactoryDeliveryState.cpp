#include "RuntimeFactoryDeliveryState.h"

#include "IniFile.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>

namespace {

constexpr std::size_t maximumFactoryDeliveryLeases = 4096;
constexpr std::size_t maximumCanonicalOwnerBytes = 256;
constexpr std::uint64_t fnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t fnvPrime = 1099511628211ULL;

bool asciiWhitespace(char value) {
    return value == ' '
        || value == '\t'
        || value == '\r'
        || value == '\n'
        || value == '\f'
        || value == '\v';
}

char lowerAsciiCharacter(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](char character) {
            return lowerAsciiCharacter(character);
        }
    );
    return value;
}

bool validCanonicalOwner(const std::string& owner) {
    return !owner.empty()
        && owner.size() <= maximumCanonicalOwnerBytes
        && !asciiWhitespace(owner.front())
        && !asciiWhitespace(owner.back());
}

bool validCell(const RuntimeFactoryDeliveryCell& cell) {
    return cell.x >= 0 && cell.y >= 0;
}

bool validDomain(RuntimeFactoryDeliveryDomain domain) {
    return domain == RuntimeFactoryDeliveryDomain::Land
        || domain == RuntimeFactoryDeliveryDomain::Naval;
}

bool validDeliveryLane(RuntimeProductionLane lane) {
    return lane == RuntimeProductionLane::Vehicles
        || lane == RuntimeProductionLane::Ships;
}

bool validLease(const RuntimeFactoryDeliveryLease& lease) {
    if (!validCanonicalOwner(lease.canonicalOwner)
        || lease.hostId < 0
        || lease.producedObjectId < 0
        || lease.producedObjectId == lease.hostId
        || lease.orderSerial == 0
        || lease.acquiredTick < 0
        || !validCell(lease.rallyCell)
        || !validDomain(lease.domain)
        || !validDeliveryLane(lease.lane)) {
        return false;
    }
    if (lease.domain == RuntimeFactoryDeliveryDomain::Land) {
        return lease.dockIndex == -1
            && lease.lane == RuntimeProductionLane::Vehicles;
    }
    return lease.dockIndex == 0
        && lease.lane == RuntimeProductionLane::Ships;
}

bool leaseLess(
    const RuntimeFactoryDeliveryLease& left,
    const RuntimeFactoryDeliveryLease& right
) {
    return std::tie(
        left.canonicalOwner,
        left.hostId,
        left.dockIndex,
        left.orderSerial,
        left.producedObjectId
    ) < std::tie(
        right.canonicalOwner,
        right.hostId,
        right.dockIndex,
        right.orderSerial,
        right.producedObjectId
    );
}

template <typename Predicate>
std::vector<RuntimeFactoryDeliveryRelease> releaseMatching(
    std::vector<RuntimeFactoryDeliveryLease>& leases,
    Predicate predicate,
    RuntimeFactoryDeliveryReleaseReason reason
) {
    std::vector<RuntimeFactoryDeliveryLease> retained;
    std::vector<RuntimeFactoryDeliveryRelease> released;
    retained.reserve(leases.size());
    released.reserve(leases.size());
    for (const RuntimeFactoryDeliveryLease& lease : leases) {
        if (predicate(lease)) {
            released.push_back({lease, reason});
        } else {
            retained.push_back(lease);
        }
    }
    std::sort(retained.begin(), retained.end(), leaseLess);
    std::sort(
        released.begin(),
        released.end(),
        [](const auto& left, const auto& right) {
            return leaseLess(left.lease, right.lease);
        }
    );
    leases = std::move(retained);
    return released;
}

const char* laneName(RuntimeProductionLane lane) {
    switch (lane) {
    case RuntimeProductionLane::Vehicles: return "Vehicles";
    case RuntimeProductionLane::Ships: return "Ships";
    default: return "Invalid";
    }
}

bool parseLane(
    const std::string& value,
    RuntimeProductionLane& output
) {
    const std::string lowered = lowerAscii(value);
    if (lowered == "vehicles") {
        output = RuntimeProductionLane::Vehicles;
        return true;
    }
    if (lowered == "ships") {
        output = RuntimeProductionLane::Ships;
        return true;
    }
    return false;
}

bool parseDomain(
    const std::string& value,
    RuntimeFactoryDeliveryDomain& output
) {
    const std::string lowered = lowerAscii(value);
    if (lowered == "land") {
        output = RuntimeFactoryDeliveryDomain::Land;
        return true;
    }
    if (lowered == "naval") {
        output = RuntimeFactoryDeliveryDomain::Naval;
        return true;
    }
    return false;
}

bool parseIntStrict(const std::string& value, int& output) {
    if (value.empty()) {
        return false;
    }
    const char* begin = value.data();
    const char* end = begin + value.size();
    int parsed = 0;
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    output = parsed;
    return true;
}

bool parseUint64Strict(
    const std::string& value,
    std::uint64_t& output
) {
    if (value.empty()) {
        return false;
    }
    const char* begin = value.data();
    const char* end = begin + value.size();
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    output = parsed;
    return true;
}

bool parseBoolStrict(const std::string& value, bool& output) {
    const std::string lowered = lowerAscii(value);
    if (lowered == "yes" || lowered == "true" || lowered == "1") {
        output = true;
        return true;
    }
    if (lowered == "no" || lowered == "false" || lowered == "0") {
        output = false;
        return true;
    }
    return false;
}

std::string hexEncode(const std::string& value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string output;
    output.reserve(value.size() * 2U);
    for (const unsigned char character : value) {
        output.push_back(digits[character >> 4]);
        output.push_back(digits[character & 0x0f]);
    }
    return output;
}

int hexNibble(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

bool hexDecode(const std::string& value, std::string& output) {
    if ((value.size() % 2U) != 0U
        || value.size() > maximumCanonicalOwnerBytes * 2U) {
        return false;
    }
    output.clear();
    output.reserve(value.size() / 2U);
    for (std::size_t index = 0; index < value.size(); index += 2U) {
        const int high = hexNibble(value[index]);
        const int low = hexNibble(value[index + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        output.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

std::uint64_t hashText(const std::string& text) {
    std::uint64_t hash = fnvOffset;
    for (const unsigned char character : text) {
        hash ^= static_cast<std::uint64_t>(character);
        hash *= fnvPrime;
    }
    return hash;
}

} // namespace

const char* runtimeFactoryDeliveryDomainName(
    RuntimeFactoryDeliveryDomain domain
) {
    switch (domain) {
    case RuntimeFactoryDeliveryDomain::Land: return "Land";
    case RuntimeFactoryDeliveryDomain::Naval: return "Naval";
    }
    return "Invalid";
}

const char* runtimeFactoryDeliveryAcquireVetoName(
    RuntimeFactoryDeliveryAcquireVeto veto
) {
    switch (veto) {
    case RuntimeFactoryDeliveryAcquireVeto::None: return "none";
    case RuntimeFactoryDeliveryAcquireVeto::InvalidRequest:
        return "invalid factory delivery request";
    case RuntimeFactoryDeliveryAcquireVeto::CapacityFull:
        return "factory delivery lease limit reached";
    case RuntimeFactoryDeliveryAcquireVeto::RepairDockClaimed:
        return "factory delivery dock is claimed by repair";
    case RuntimeFactoryDeliveryAcquireVeto::FactoryDockClaimed:
        return "factory delivery dock is already claimed";
    case RuntimeFactoryDeliveryAcquireVeto::OrderAlreadyLeased:
        return "factory delivery order is already leased";
    case RuntimeFactoryDeliveryAcquireVeto::ObjectAlreadyLeased:
        return "factory delivery object is already leased";
    }
    return "unknown factory delivery veto";
}

const char* runtimeFactoryDeliveryReleaseReasonName(
    RuntimeFactoryDeliveryReleaseReason reason
) {
    switch (reason) {
    case RuntimeFactoryDeliveryReleaseReason::ExplicitObject:
        return "explicit object release";
    case RuntimeFactoryDeliveryReleaseReason::ExplicitHost:
        return "explicit host release";
    case RuntimeFactoryDeliveryReleaseReason::ExplicitOrder:
        return "explicit order release";
    case RuntimeFactoryDeliveryReleaseReason::HostUnavailable:
        return "factory delivery host unavailable";
    case RuntimeFactoryDeliveryReleaseReason::HostMismatch:
        return "factory delivery host mismatch";
    case RuntimeFactoryDeliveryReleaseReason::ObjectUnavailable:
        return "factory delivery object unavailable";
    case RuntimeFactoryDeliveryReleaseReason::ObjectMismatch:
        return "factory delivery object mismatch";
    case RuntimeFactoryDeliveryReleaseReason::OrderUnavailable:
        return "factory delivery order unavailable";
    case RuntimeFactoryDeliveryReleaseReason::OrderMismatch:
        return "factory delivery order mismatch";
    case RuntimeFactoryDeliveryReleaseReason::BlockedOrderNotRetained:
        return "blocked factory order is not retained by host contract";
    }
    return "unknown factory delivery release";
}

void RuntimeFactoryDeliveryState::reset() {
    leases_.clear();
    pendingFreeUnits_.clear();
}

bool RuntimeFactoryDeliveryState::deferFreeUnit(RuntimePendingFreeUnit grant) {
    if (grant.hostId < 0 || !validCanonicalOwner(grant.owner)
        || !validCanonicalOwner(grant.buildingType)
        || !validCanonicalOwner(grant.unitType)
        || pendingFreeUnits_.size() >= maximumFactoryDeliveryLeases
        || std::any_of(pendingFreeUnits_.begin(), pendingFreeUnits_.end(),
            [&](const auto& existing) { return existing.hostId == grant.hostId; })) {
        return false;
    }
    pendingFreeUnits_.push_back(std::move(grant));
    std::sort(pendingFreeUnits_.begin(), pendingFreeUnits_.end(),
        [](const auto& a, const auto& b) { return a.hostId < b.hostId; });
    return true;
}

void RuntimeFactoryDeliveryState::completeFreeUnit(int hostId) {
    std::erase_if(pendingFreeUnits_, [hostId](const auto& grant) {
        return grant.hostId == hostId;
    });
}

const std::vector<RuntimePendingFreeUnit>&
RuntimeFactoryDeliveryState::pendingFreeUnits() const { return pendingFreeUnits_; }

RuntimeFactoryDeliveryAcquireResult RuntimeFactoryDeliveryState::acquire(
    const RuntimeFactoryDeliveryAcquireRequest& request
) {
    RuntimeFactoryDeliveryAcquireResult result;
    result.lease = request.lease;
    const auto reject = [&](RuntimeFactoryDeliveryAcquireVeto veto) {
        result.acquired = false;
        result.veto = veto;
        result.reason = runtimeFactoryDeliveryAcquireVetoName(veto);
        return result;
    };

    if (!validLease(request.lease)) {
        return reject(RuntimeFactoryDeliveryAcquireVeto::InvalidRequest);
    }
    if (request.repairDockClaimed) {
        return reject(RuntimeFactoryDeliveryAcquireVeto::RepairDockClaimed);
    }
    if (leases_.size() >= maximumFactoryDeliveryLeases) {
        return reject(RuntimeFactoryDeliveryAcquireVeto::CapacityFull);
    }
    if (std::any_of(
            leases_.begin(),
            leases_.end(),
            [&](const RuntimeFactoryDeliveryLease& lease) {
                return lease.hostId == request.lease.hostId
                    && lease.dockIndex == request.lease.dockIndex;
            }
        )) {
        return reject(RuntimeFactoryDeliveryAcquireVeto::FactoryDockClaimed);
    }
    if (std::any_of(
            leases_.begin(),
            leases_.end(),
            [&](const RuntimeFactoryDeliveryLease& lease) {
                return lease.canonicalOwner
                        == request.lease.canonicalOwner
                    && lease.orderSerial == request.lease.orderSerial;
            }
        )) {
        return reject(RuntimeFactoryDeliveryAcquireVeto::OrderAlreadyLeased);
    }
    if (std::any_of(
            leases_.begin(),
            leases_.end(),
            [&](const RuntimeFactoryDeliveryLease& lease) {
                return lease.producedObjectId
                    == request.lease.producedObjectId;
            }
        )) {
        return reject(RuntimeFactoryDeliveryAcquireVeto::ObjectAlreadyLeased);
    }

    leases_.push_back(request.lease);
    sortState();
    result.acquired = true;
    result.veto = RuntimeFactoryDeliveryAcquireVeto::None;
    result.reason = "factory delivery lease acquired";
    return result;
}

std::vector<RuntimeFactoryDeliveryRelease>
RuntimeFactoryDeliveryState::releaseByObject(int producedObjectId) {
    return releaseMatching(
        leases_,
        [producedObjectId](const RuntimeFactoryDeliveryLease& lease) {
            return lease.producedObjectId == producedObjectId;
        },
        RuntimeFactoryDeliveryReleaseReason::ExplicitObject
    );
}

std::vector<RuntimeFactoryDeliveryRelease>
RuntimeFactoryDeliveryState::releaseByHost(int hostId) {
    completeFreeUnit(hostId);
    return releaseMatching(
        leases_,
        [hostId](const RuntimeFactoryDeliveryLease& lease) {
            return lease.hostId == hostId;
        },
        RuntimeFactoryDeliveryReleaseReason::ExplicitHost
    );
}

std::vector<RuntimeFactoryDeliveryRelease>
RuntimeFactoryDeliveryState::releaseByOrder(
    std::string_view canonicalOwner,
    RuntimeProductionLane lane,
    std::uint64_t orderSerial
) {
    return releaseMatching(
        leases_,
        [&](const RuntimeFactoryDeliveryLease& lease) {
            return lease.canonicalOwner == canonicalOwner
                && lease.lane == lane
                && lease.orderSerial == orderSerial;
        },
        RuntimeFactoryDeliveryReleaseReason::ExplicitOrder
    );
}

bool RuntimeFactoryDeliveryState::claimsDock(
    int hostId,
    int dockIndex
) const {
    return std::any_of(
        leases_.begin(),
        leases_.end(),
        [hostId, dockIndex](const RuntimeFactoryDeliveryLease& lease) {
            return lease.hostId == hostId
                && lease.dockIndex == dockIndex;
        }
    );
}

RuntimeFactoryDeliveryReconcileReport
RuntimeFactoryDeliveryState::reconcile(
    const RuntimeFactoryDeliveryReconcileInput& input
) {
    RuntimeFactoryDeliveryReconcileReport report;
    std::map<int, const RuntimeFactoryDeliveryHostSnapshot*> hosts;
    std::map<int, const RuntimeFactoryDeliveryObjectSnapshot*> objects;
    using OrderKey = std::pair<std::string, std::uint64_t>;
    std::map<OrderKey, const RuntimeFactoryDeliveryOrderSnapshot*> orders;

    for (const RuntimeFactoryDeliveryHostSnapshot& host : input.hosts) {
        if (!validCanonicalOwner(host.canonicalOwner)
            || host.hostId < 0
            || !validDomain(host.domain)
            || !hosts.emplace(host.hostId, &host).second) {
            report.error = "invalid or duplicate factory host snapshot";
            return report;
        }
    }
    for (const RuntimeFactoryDeliveryObjectSnapshot& object : input.objects) {
        if (!validCanonicalOwner(object.canonicalOwner)
            || object.objectId < 0
            || !objects.emplace(object.objectId, &object).second) {
            report.error = "invalid or duplicate factory object snapshot";
            return report;
        }
    }
    for (const RuntimeFactoryDeliveryOrderSnapshot& order : input.orders) {
        if (!validCanonicalOwner(order.canonicalOwner)
            || !validDeliveryLane(order.lane)
            || order.orderSerial == 0
            || !orders.emplace(
                    OrderKey{order.canonicalOwner, order.orderSerial},
                    &order
                ).second) {
            report.error = "invalid or duplicate factory order snapshot";
            return report;
        }
    }

    std::vector<RuntimeFactoryDeliveryLease> candidates = leases_;
    std::sort(candidates.begin(), candidates.end(), leaseLess);
    std::vector<RuntimeFactoryDeliveryLease> retained;
    retained.reserve(candidates.size());
    for (const RuntimeFactoryDeliveryLease& lease : candidates) {
        RuntimeFactoryDeliveryReleaseReason releaseReason =
            RuntimeFactoryDeliveryReleaseReason::OrderUnavailable;
        bool keep = false;
        const auto hostIt = hosts.find(lease.hostId);
        if (hostIt == hosts.end() || !hostIt->second->available) {
            releaseReason =
                RuntimeFactoryDeliveryReleaseReason::HostUnavailable;
        } else if (hostIt->second->canonicalOwner
                != lease.canonicalOwner
            || hostIt->second->domain != lease.domain) {
            releaseReason = RuntimeFactoryDeliveryReleaseReason::HostMismatch;
        } else {
            const auto objectIt = objects.find(lease.producedObjectId);
            if (objectIt != objects.end()) {
                if (!objectIt->second->available) {
                    releaseReason =
                        RuntimeFactoryDeliveryReleaseReason::ObjectUnavailable;
                } else if (objectIt->second->canonicalOwner
                        != lease.canonicalOwner) {
                    releaseReason =
                        RuntimeFactoryDeliveryReleaseReason::ObjectMismatch;
                } else {
                    keep = true;
                }
            } else {
                const auto orderIt = orders.find(OrderKey{
                    lease.canonicalOwner,
                    lease.orderSerial
                });
                if (orderIt == orders.end()) {
                    releaseReason =
                        RuntimeFactoryDeliveryReleaseReason::OrderUnavailable;
                } else if (orderIt->second->canonicalOwner
                            != lease.canonicalOwner
                        || orderIt->second->lane != lease.lane) {
                    releaseReason =
                        RuntimeFactoryDeliveryReleaseReason::OrderMismatch;
                } else if (!orderIt->second->blocked) {
                    releaseReason =
                        RuntimeFactoryDeliveryReleaseReason::OrderUnavailable;
                } else if (!hostIt->second->retainBlockedOrder) {
                    releaseReason = RuntimeFactoryDeliveryReleaseReason::
                        BlockedOrderNotRetained;
                } else {
                    keep = true;
                }
            }
        }

        if (keep) {
            retained.push_back(lease);
            ++report.retainedLeases;
        } else {
            report.releases.push_back({lease, releaseReason});
            ++report.releasedLeases;
        }
    }

    leases_ = std::move(retained);
    report.accepted = true;
    return report;
}

void RuntimeFactoryDeliveryState::save(std::ostream& output) const {
    std::vector<RuntimeFactoryDeliveryLease> leases = leases_;
    std::sort(leases.begin(), leases.end(), leaseLess);
    output << "[FactoryDeliveryRuntime]\n";
    output << "Version=2\n";
    output << "LeaseCount=" << leases.size() << "\n";
    output << "FreeUnitCount=" << pendingFreeUnits_.size() << "\n\n";
    for (std::size_t index = 0; index < pendingFreeUnits_.size(); ++index) {
        const auto& grant = pendingFreeUnits_[index];
        output << "[FactoryDeliveryRuntime.FreeUnit." << index << "]\n"
               << "HostId=" << grant.hostId << "\n"
               << "OwnerHex=" << hexEncode(grant.owner) << "\n"
               << "BuildingTypeHex=" << hexEncode(grant.buildingType) << "\n"
               << "UnitTypeHex=" << hexEncode(grant.unitType) << "\n\n";
    }
    for (std::size_t index = 0; index < leases.size(); ++index) {
        const RuntimeFactoryDeliveryLease& lease = leases[index];
        output << "[FactoryDeliveryRuntime.Lease." << index << "]\n";
        output << "CanonicalOwnerHex="
               << hexEncode(lease.canonicalOwner) << "\n";
        output << "HostId=" << lease.hostId << "\n";
        output << "DockIndex=" << lease.dockIndex << "\n";
        output << "Lane=" << laneName(lease.lane) << "\n";
        output << "OrderSerial=" << lease.orderSerial << "\n";
        output << "ProducedObjectId=" << lease.producedObjectId << "\n";
        output << "AcquiredTick=" << lease.acquiredTick << "\n";
        output << "RallyX=" << lease.rallyCell.x << "\n";
        output << "RallyY=" << lease.rallyCell.y << "\n";
        output << "RallyBridge="
               << (lease.rallyCell.onBridge ? "yes" : "no") << "\n";
        output << "Domain="
               << runtimeFactoryDeliveryDomainName(lease.domain)
               << "\n\n";
    }
}

bool RuntimeFactoryDeliveryState::load(
    const IniFile& ini,
    std::string* errorMessage
) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (ini.getSection("FactoryDeliveryRuntime") == nullptr) {
        reset();
        return true;
    }
    const auto fail = [&](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    const auto value = [&](const std::string& section,
                           const std::string& key) {
        return ini.getValue(section, key, "");
    };

    int version = 0;
    int leaseCount = 0;
    if (!parseIntStrict(
            value("FactoryDeliveryRuntime", "Version"),
            version
        )
        || (version != 1 && version != 2)
        || !parseIntStrict(
            value("FactoryDeliveryRuntime", "LeaseCount"),
            leaseCount
        )
        || leaseCount < 0
        || static_cast<std::size_t>(leaseCount)
            > maximumFactoryDeliveryLeases) {
        return fail("factory delivery save has invalid header values");
    }

    std::vector<RuntimeFactoryDeliveryLease> loaded;
    loaded.reserve(static_cast<std::size_t>(leaseCount));
    std::set<std::pair<int, int>> hostDocks;
    std::set<std::pair<std::string, std::uint64_t>> orderSerials;
    std::set<int> objectIds;
    for (int index = 0; index < leaseCount; ++index) {
        const std::string section = "FactoryDeliveryRuntime.Lease."
            + std::to_string(index);
        RuntimeFactoryDeliveryLease lease;
        bool rallyBridge = false;
        if (ini.getSection(section) == nullptr
            || !hexDecode(
                value(section, "CanonicalOwnerHex"),
                lease.canonicalOwner
            )
            || !parseIntStrict(value(section, "HostId"), lease.hostId)
            || !parseIntStrict(
                value(section, "DockIndex"),
                lease.dockIndex
            )
            || !parseLane(value(section, "Lane"), lease.lane)
            || !parseUint64Strict(
                value(section, "OrderSerial"),
                lease.orderSerial
            )
            || !parseIntStrict(
                value(section, "ProducedObjectId"),
                lease.producedObjectId
            )
            || !parseIntStrict(
                value(section, "AcquiredTick"),
                lease.acquiredTick
            )
            || !parseIntStrict(
                value(section, "RallyX"),
                lease.rallyCell.x
            )
            || !parseIntStrict(
                value(section, "RallyY"),
                lease.rallyCell.y
            )
            || !parseBoolStrict(
                value(section, "RallyBridge"),
                rallyBridge
            )
            || !parseDomain(value(section, "Domain"), lease.domain)) {
            return fail("factory delivery save has invalid lease ["
                + section + "]");
        }
        lease.rallyCell.onBridge = rallyBridge;
        if (!validLease(lease)
            || !hostDocks.insert({lease.hostId, lease.dockIndex}).second
            || !orderSerials.insert({
                    lease.canonicalOwner,
                    lease.orderSerial
                }).second
            || !objectIds.insert(lease.producedObjectId).second) {
            return fail("factory delivery save has invalid or duplicate lease ["
                + section + "]");
        }
        loaded.push_back(std::move(lease));
    }

    RuntimeFactoryDeliveryState grants;
    int grantCount = 0;
    if (version >= 2 && (!parseIntStrict(value("FactoryDeliveryRuntime", "FreeUnitCount"), grantCount)
        || grantCount < 0 || grantCount > static_cast<int>(maximumFactoryDeliveryLeases))) {
        return fail("factory delivery save has invalid free unit count");
    }
    for (int index = 0; index < grantCount; ++index) {
        const std::string section = "FactoryDeliveryRuntime.FreeUnit." + std::to_string(index);
        RuntimePendingFreeUnit grant;
        if (!parseIntStrict(value(section, "HostId"), grant.hostId)
            || !hexDecode(value(section, "OwnerHex"), grant.owner)
            || !hexDecode(value(section, "BuildingTypeHex"), grant.buildingType)
            || !hexDecode(value(section, "UnitTypeHex"), grant.unitType)
            || !grants.deferFreeUnit(std::move(grant))) {
            return fail("factory delivery save has invalid or duplicate free unit grant");
        }
    }
    std::sort(loaded.begin(), loaded.end(), leaseLess);
    leases_ = std::move(loaded);
    pendingFreeUnits_ = std::move(grants.pendingFreeUnits_);
    return true;
}

std::uint64_t RuntimeFactoryDeliveryState::stateHash() const {
    std::ostringstream stream;
    save(stream);
    return hashText(
        std::string("RuntimeFactoryDeliveryState/v2\n") + stream.str()
    );
}

const std::vector<RuntimeFactoryDeliveryLease>&
RuntimeFactoryDeliveryState::leases() const {
    return leases_;
}

void RuntimeFactoryDeliveryState::sortState() {
    std::sort(leases_.begin(), leases_.end(), leaseLess);
}
