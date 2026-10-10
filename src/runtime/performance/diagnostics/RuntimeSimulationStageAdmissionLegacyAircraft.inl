#pragma once
#include "simulation/ai/RuntimeAircraftServiceSystem.h"

#include "core/io/IniFile.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

namespace RuntimeSimulationStageAdmissionLegacyAircraft {
class RuntimeAircraftServiceSystemState {
public:
    RuntimeAircraftServiceSystemState();
    explicit RuntimeAircraftServiceSystemState(int fixedTickHz);

    void reset(int fixedTickHz = 30);
    int fixedTickHz() const;

    RuntimeAircraftServiceReconcileReport reconcile(
        const RuntimeAircraftServiceContext& context
    );
    std::vector<RuntimeAircraftServiceAction> tick(
        const RuntimeAircraftServiceContext& context,
        int currentTick
    );

    const std::vector<RuntimeAircraftServiceHostState>& hosts() const;
    const std::vector<RuntimeAircraftServiceDockBinding>& dockBindings() const;
    int remainingReloadTicks(int hostId) const;
    int remainingRepairTicks(int hostId) const;
    int lastAdvancedTick() const;

    void save(std::ostream& output) const;
    bool load(
        const IniFile& ini,
        const RuntimeAircraftServiceContext& context,
        std::string* errorMessage = nullptr,
        RuntimeAircraftServiceReconcileReport* reconcileReport = nullptr
    );
    std::uint64_t stateHash() const;

private:
    int fixedTickHz_ = 30;
    int lastAdvancedTick_ = -1;
    std::vector<RuntimeAircraftServiceHostState> hosts_;
    std::vector<RuntimeAircraftServiceDockBinding> dockBindings_;

    void sortState();
};
namespace {

constexpr std::size_t maximumSavedHosts = 4096;
constexpr std::size_t maximumSavedBindings = 16384;
constexpr std::size_t maximumPulsesPerTick = 262144;
constexpr int maximumDockIndex = 65535;
constexpr std::uint64_t fnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t fnvPrime = 1099511628211ULL;

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

bool equalsAsciiIgnoreCase(
    const std::string& left,
    const std::string& right
) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (lowerAsciiCharacter(left[index])
            != lowerAsciiCharacter(right[index])) {
            return false;
        }
    }
    return true;
}

const WorldObjectInstance* findObject(
    const RuntimeAircraftServiceContext& context,
    int objectId
) {
    if (context.objects == nullptr) {
        return nullptr;
    }
    for (const WorldObjectInstance& object : *context.objects) {
        if (object.instanceId == objectId) {
            return &object;
        }
    }
    return nullptr;
}

const BuildingType* liveServiceHostRules(
    const RuntimeAircraftServiceContext& context,
    const WorldObjectInstance* object
) {
    if (context.rules == nullptr
        || object == nullptr
        || object->instanceId < 0
        || object->kind != WorldObjectKind::Structure
        || !object->alive
        || object->health <= 0
        || object->rubble) {
        return nullptr;
    }
    const BuildingType* building =
        context.rules->findBuildingType(object->type);
    return building != nullptr
            && building->unitReload
            && building->numberOfDocks > 0
        ? building
        : nullptr;
}

const UnitType* aircraftServiceRulesForObject(
    const RuntimeAircraftServiceContext& context,
    const WorldObjectInstance* object
) {
    if (context.rules == nullptr
        || object == nullptr
        || object->kind != WorldObjectKind::Unit) {
        return nullptr;
    }

    // Preserve the object's exact rules namespace. Objects authored as
    // aircraft use AircraftTypes. Ordinary Unit objects are vehicle-first,
    // matching GameWorld hydration; a VehicleTypes registration may use an
    // aircraft pad only when [General] PadAircraft names it. The final
    // AircraftTypes fallback keeps legacy/runtime-created aircraft whose
    // object record predates the explicit authoredAircraft discriminator.
    if (object->authoredAircraft) {
        return context.rules->findUnitType("Aircraft", object->type);
    }
    if (const UnitType* vehicle =
            context.rules->findUnitType("Vehicle", object->type)) {
        return context.rules->isPadAircraft(object->type)
            ? vehicle
            : nullptr;
    }
    return context.rules->findUnitType("Aircraft", object->type);
}

const UnitType* liveAircraftRules(
    const RuntimeAircraftServiceContext& context,
    const WorldObjectInstance* object
) {
    if (context.rules == nullptr
        || object == nullptr
        || object->instanceId < 0
        || object->kind != WorldObjectKind::Unit
        || !object->alive
        || object->health <= 0
        || object->airborne) {
        return nullptr;
    }
    return aircraftServiceRulesForObject(context, object);
}

bool ownersAreAllied(
    const RuntimeAircraftServiceContext& context,
    const std::string& left,
    const std::string& right
) {
    if (equalsAsciiIgnoreCase(left, right)) {
        return true;
    }
    return context.areAllied && context.areAllied(left, right);
}

int serviceIntervalTicks(int fixedTickHz, double reloadRateMinutes) {
    if (!std::isfinite(reloadRateMinutes)
        || reloadRateMinutes <= 0.0) {
        return 1;
    }
    const long double ticks =
        static_cast<long double>(reloadRateMinutes)
        * 60.0L
        * static_cast<long double>(std::max(1, fixedTickHz));
    if (ticks >= static_cast<long double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return std::max(
        1,
        static_cast<int>(std::ceil(ticks - 1.0e-12L))
    );
}

struct RepairCharge {
    int healedHitPoints = 0;
    int creditCost = 0;
};

RepairCharge calculateRepairCharge(
    const UnitType& aircraft,
    const SpecialMechanismRules& rules,
    int currentHealth,
    int availableCredits,
    int maximumHealth
) {
    RepairCharge result;
    const int maxHealth = std::max(1, maximumHealth);
    const int missingHealth = std::max(0, maxHealth - currentHealth);
    const int repairStep = std::min(
        missingHealth,
        std::max(0, rules.repairStep)
    );
    if (repairStep <= 0) {
        return result;
    }
    if (!std::isfinite(rules.repairPercent)
        || rules.repairPercent <= 0.0
        || aircraft.cost <= 0) {
        result.healedHitPoints = repairStep;
        return result;
    }

    const long double costPerHitPoint =
        static_cast<long double>(rules.repairPercent)
        * static_cast<long double>(aircraft.cost)
        / static_cast<long double>(maxHealth);
    if (!std::isfinite(static_cast<double>(costPerHitPoint))
        || costPerHitPoint <= 0.0L
        || availableCredits <= 0) {
        return result;
    }

    const long double authoredStepCost = costPerHitPoint
        * static_cast<long double>(std::max(0, rules.repairStep));
    long long roundedStepCost = static_cast<long long>(std::floor(
        std::min(
            authoredStepCost,
            static_cast<long double>(std::numeric_limits<int>::max())
        )
    ));
    roundedStepCost = std::max<long long>(1, roundedStepCost);
    const int paidCost = std::min(
        std::max(0, availableCredits),
        static_cast<int>(roundedStepCost)
    );
    const long double affordable = static_cast<long double>(paidCost)
        / costPerHitPoint;
    const int affordableHitPoints = affordable
            >= static_cast<long double>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(std::floor(affordable + 1.0e-12L));
    result.healedHitPoints = std::min(repairStep, affordableHitPoints);
    if (result.healedHitPoints <= 0) {
        return {};
    }
    result.creditCost = paidCost;
    return result;
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
    if ((value.size() % 2U) != 0U || value.size() > 8192U) {
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

bool parseIntStrict(const std::string& value, int& output) {
    if (value.empty()) {
        return false;
    }
    const char* first = value.data();
    const char* last = first + value.size();
    const std::from_chars_result parsed = std::from_chars(first, last, output);
    return parsed.ec == std::errc{} && parsed.ptr == last;
}

std::uint64_t hashText(std::string_view value) {
    std::uint64_t hash = fnvOffset;
    for (const unsigned char character : value) {
        hash ^= character;
        hash *= fnvPrime;
    }
    return hash;
}

using BindingKey = std::tuple<int, int, int>;

BindingKey bindingKey(const RuntimeAircraftServiceDockBinding& binding) {
    return {binding.hostId, binding.dockIndex, binding.aircraftId};
}

} // namespace

RuntimeAircraftServiceSystemState::RuntimeAircraftServiceSystemState() =
    default;

RuntimeAircraftServiceSystemState::RuntimeAircraftServiceSystemState(
    int fixedTickHz
) {
    reset(fixedTickHz);
}

void RuntimeAircraftServiceSystemState::reset(int fixedTickHz) {
    fixedTickHz_ = std::clamp(fixedTickHz, 1, 1000);
    lastAdvancedTick_ = -1;
    hosts_.clear();
    dockBindings_.clear();
}

int RuntimeAircraftServiceSystemState::fixedTickHz() const {
    return fixedTickHz_;
}

RuntimeAircraftServiceReconcileReport
RuntimeAircraftServiceSystemState::reconcile(
    const RuntimeAircraftServiceContext& context
) {
    RuntimeAircraftServiceReconcileReport report;
    if (context.rules == nullptr || context.objects == nullptr) {
        return report;
    }
    report.contextValid = true;

    struct LiveHost {
        const WorldObjectInstance* object = nullptr;
        const BuildingType* rules = nullptr;
    };
    std::map<int, LiveHost> liveHosts;
    for (const WorldObjectInstance& object : *context.objects) {
        const BuildingType* building = liveServiceHostRules(context, &object);
        if (building == nullptr) {
            continue;
        }
        liveHosts.emplace(object.instanceId, LiveHost{&object, building});
    }

    sortState();
    std::map<int, RuntimeAircraftServiceHostState> oldHosts;
    for (const RuntimeAircraftServiceHostState& host : hosts_) {
        oldHosts.emplace(host.hostId, host);
    }
    std::vector<RuntimeAircraftServiceHostState> reconciledHosts;
    reconciledHosts.reserve(std::min(maximumSavedHosts, liveHosts.size()));
    for (const auto& [hostId, live] : liveHosts) {
        if (reconciledHosts.size() >= maximumSavedHosts) {
            ++report.skippedHostsByLimit;
            continue;
        }
        RuntimeAircraftServiceHostState host;
        const auto old = oldHosts.find(hostId);
        if (old == oldHosts.end()) {
            host.hostId = hostId;
            host.owner = live.object->owner;
            host.type = live.object->type;
            ++report.addedHosts;
        } else if (!equalsAsciiIgnoreCase(old->second.owner, live.object->owner)
            || !equalsAsciiIgnoreCase(old->second.type, live.object->type)) {
            host.hostId = hostId;
            host.owner = live.object->owner;
            host.type = live.object->type;
            ++report.resetHosts;
            oldHosts.erase(old);
        } else {
            host = old->second;
            host.owner = live.object->owner;
            host.type = live.object->type;
            ++report.retainedHosts;
            oldHosts.erase(old);
        }
        reconciledHosts.push_back(std::move(host));
    }
    report.removedHosts = oldHosts.size();
    hosts_ = std::move(reconciledHosts);

    std::set<int> activeHostIds;
    for (const RuntimeAircraftServiceHostState& host : hosts_) {
        activeHostIds.insert(host.hostId);
    }

    struct Candidate {
        const WorldObjectInstance* host = nullptr;
        const WorldObjectInstance* aircraft = nullptr;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(context.objects->size());
    for (const WorldObjectInstance& aircraft : *context.objects) {
        if (aircraft.dockedToObjectId < 0 || aircraft.dockIndex < 0) {
            continue;
        }
        const auto host = liveHosts.find(aircraft.dockedToObjectId);
        if (host == liveHosts.end()
            || activeHostIds.find(aircraft.dockedToObjectId)
                == activeHostIds.end()
            || aircraft.dockIndex > maximumDockIndex
            || aircraft.dockIndex >= host->second.rules->numberOfDocks
            || liveAircraftRules(context, &aircraft) == nullptr
            || !ownersAreAllied(
                context,
                host->second.object->owner,
                aircraft.owner
            )) {
            continue;
        }
        candidates.push_back({host->second.object, &aircraft});
    }
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate& left, const Candidate& right) {
            return std::tie(
                left.host->instanceId,
                left.aircraft->dockIndex,
                left.aircraft->instanceId
            ) < std::tie(
                right.host->instanceId,
                right.aircraft->dockIndex,
                right.aircraft->instanceId
            );
        }
    );

    std::set<std::pair<int, int>> occupiedPads;
    std::set<int> boundAircraft;
    std::vector<RuntimeAircraftServiceDockBinding> reconciledBindings;
    reconciledBindings.reserve(
        std::min(maximumSavedBindings, candidates.size())
    );
    for (const Candidate& candidate : candidates) {
        const std::pair<int, int> pad{
            candidate.host->instanceId,
            candidate.aircraft->dockIndex
        };
        if (occupiedPads.find(pad) != occupiedPads.end()) {
            ++report.duplicateDockBindings;
            continue;
        }
        if (boundAircraft.find(candidate.aircraft->instanceId)
            != boundAircraft.end()) {
            ++report.duplicateAircraftBindings;
            continue;
        }
        if (reconciledBindings.size() >= maximumSavedBindings) {
            ++report.skippedBindingsByLimit;
            continue;
        }
        occupiedPads.insert(pad);
        boundAircraft.insert(candidate.aircraft->instanceId);
        RuntimeAircraftServiceDockBinding binding;
        binding.hostId = candidate.host->instanceId;
        binding.aircraftId = candidate.aircraft->instanceId;
        binding.dockIndex = candidate.aircraft->dockIndex;
        binding.hostOwner = candidate.host->owner;
        binding.aircraftOwner = candidate.aircraft->owner;
        binding.hostType = candidate.host->type;
        binding.aircraftType = candidate.aircraft->type;
        reconciledBindings.push_back(std::move(binding));
    }

    std::set<BindingKey> oldBindingKeys;
    for (const RuntimeAircraftServiceDockBinding& binding : dockBindings_) {
        oldBindingKeys.insert(bindingKey(binding));
    }
    for (const RuntimeAircraftServiceDockBinding& binding
         : reconciledBindings) {
        if (oldBindingKeys.erase(bindingKey(binding)) != 0U) {
            ++report.retainedBindings;
        } else {
            ++report.addedBindings;
        }
    }
    report.removedBindings = oldBindingKeys.size();
    dockBindings_ = std::move(reconciledBindings);
    sortState();
    return report;
}

std::vector<RuntimeAircraftServiceAction>
RuntimeAircraftServiceSystemState::tick(
    const RuntimeAircraftServiceContext& context,
    int currentTick
) {
    const RuntimeAircraftServiceReconcileReport report = reconcile(context);
    std::vector<RuntimeAircraftServiceAction> actions;
    if (!report.contextValid
        || currentTick < 0
        || currentTick <= lastAdvancedTick_) {
        return actions;
    }

    const int previousTick = lastAdvancedTick_;
    const long long elapsedTicks = previousTick < 0
        ? 0LL
        : static_cast<long long>(currentTick)
            - static_cast<long long>(previousTick);
    const SpecialMechanismRules& mechanism =
        context.rules->getSpecialMechanismRules();
    const int interval = serviceIntervalTicks(
        fixedTickHz_,
        mechanism.reloadRateMinutes
    );

    std::map<int, int> ammoLedger;
    std::map<int, int> healthLedger;
    std::map<std::string, int> creditLedger;
    const auto economyOwner = [&](const std::string& owner) {
        std::string canonical = context.canonicalOwnerId
            ? context.canonicalOwnerId(owner)
            : std::string();
        if (canonical.empty()) {
            canonical = lowerAscii(owner);
        }
        return canonical;
    };
    const auto availableCredits = [&](const std::string& owner) {
        const std::string canonical = economyOwner(owner);
        const auto found = creditLedger.find(canonical);
        if (found != creditLedger.end()) {
            return found->second;
        }
        const int balance = context.creditBalance
            ? std::max(0, context.creditBalance(canonical))
            : 0;
        creditLedger.emplace(canonical, balance);
        return balance;
    };

    const auto validBinding = [&](const RuntimeAircraftServiceDockBinding& binding) {
        const WorldObjectInstance* host = findObject(context, binding.hostId);
        const WorldObjectInstance* aircraft = findObject(
            context,
            binding.aircraftId
        );
        const BuildingType* hostRules = liveServiceHostRules(context, host);
        const UnitType* aircraftRules = liveAircraftRules(context, aircraft);
        return host != nullptr
            && aircraft != nullptr
            && hostRules != nullptr
            && aircraftRules != nullptr
            && aircraft->dockedToObjectId == binding.hostId
            && aircraft->dockIndex == binding.dockIndex
            && binding.dockIndex >= 0
            && binding.dockIndex < hostRules->numberOfDocks
            && ownersAreAllied(context, host->owner, aircraft->owner);
    };
    const auto ammoValue = [&](const WorldObjectInstance& aircraft) {
        const auto found = ammoLedger.find(aircraft.instanceId);
        return found == ammoLedger.end()
            ? aircraft.currentAmmo
            : found->second;
    };
    const auto healthValue = [&](const WorldObjectInstance& aircraft) {
        const auto found = healthLedger.find(aircraft.instanceId);
        return found == healthLedger.end()
            ? aircraft.health
            : found->second;
    };
    const auto maximumHealth = [&](const WorldObjectInstance& aircraft, const UnitType& rules) {
        return std::max(1, context.maximumHealth ? context.maximumHealth(aircraft) : rules.strength);
    };
    const auto reloadCandidates = [&](int hostId) {
        std::vector<const RuntimeAircraftServiceDockBinding*> eligible;
        for (const RuntimeAircraftServiceDockBinding& binding : dockBindings_) {
            if (binding.hostId != hostId || !validBinding(binding)) {
                continue;
            }
            const WorldObjectInstance* aircraft = findObject(
                context,
                binding.aircraftId
            );
            const UnitType* rules = liveAircraftRules(context, aircraft);
            const int ammo = ammoValue(*aircraft);
            if (rules != nullptr
                && rules->manualReload
                && rules->ammo >= 0
                && ammo >= 0
                && ammo < rules->ammo) {
                eligible.push_back(&binding);
            }
        }
        return eligible;
    };
    const auto repairCandidates = [&](int hostId) {
        std::vector<const RuntimeAircraftServiceDockBinding*> eligible;
        for (const RuntimeAircraftServiceDockBinding& binding : dockBindings_) {
            if (binding.hostId != hostId || !validBinding(binding)) {
                continue;
            }
            const WorldObjectInstance* aircraft = findObject(
                context,
                binding.aircraftId
            );
            const UnitType* rules = liveAircraftRules(context, aircraft);
            if (rules != nullptr
                && rules->repairable
                && healthValue(*aircraft) < maximumHealth(*aircraft, *rules)) {
                eligible.push_back(&binding);
            }
        }
        return eligible;
    };
    const auto hostPowered = [&](int hostId) {
        const WorldObjectInstance* host = findObject(context, hostId);
        return liveServiceHostRules(context, host) != nullptr
            && host->poweredOperational;
    };

    struct ScheduledEvent {
        int eventTick = -1;
        std::size_t hostIndex = 0;
        RuntimeAircraftServiceActionKind kind =
            RuntimeAircraftServiceActionKind::ReloadAmmo;
    };
    struct LaterEvent {
        bool operator()(const ScheduledEvent& left,
                        const ScheduledEvent& right) const {
            return std::tie(left.eventTick, left.hostIndex, left.kind)
                > std::tie(right.eventTick, right.hostIndex, right.kind);
        }
    };
    std::priority_queue<
        ScheduledEvent,
        std::vector<ScheduledEvent>,
        LaterEvent
    > scheduled;

    const auto scheduleTimer = [&](std::size_t hostIndex,
                                   RuntimeAircraftServiceActionKind kind,
                                   int& remainingTicks,
                                   bool eligible) {
        RuntimeAircraftServiceHostState& host = hosts_[hostIndex];
        if (!hostPowered(host.hostId) || !eligible) {
            return;
        }
        if (kind == RuntimeAircraftServiceActionKind::ReloadAmmo
            && remainingTicks < 0) {
            remainingTicks = interval;
            return;
        }
        if (remainingTicks == 0) {
            scheduled.push({currentTick, hostIndex, kind});
            return;
        }
        if (previousTick < 0) {
            return;
        }
        if (static_cast<long long>(remainingTicks) <= elapsedTicks) {
            scheduled.push({
                previousTick + remainingTicks,
                hostIndex,
                kind
            });
            remainingTicks = 0;
            return;
        }
        remainingTicks -= static_cast<int>(elapsedTicks);
    };

    for (std::size_t index = 0; index < hosts_.size(); ++index) {
        RuntimeAircraftServiceHostState& host = hosts_[index];
        scheduleTimer(
            index,
            RuntimeAircraftServiceActionKind::ReloadAmmo,
            host.reloadRemainingTicks,
            !reloadCandidates(host.hostId).empty()
        );
        scheduleTimer(
            index,
            RuntimeAircraftServiceActionKind::Repair,
            host.repairRemainingTicks,
            !repairCandidates(host.hostId).empty()
        );
    }

    const auto applyReloadPulse = [&](const ScheduledEvent& event) {
        const RuntimeAircraftServiceHostState& host = hosts_[event.hostIndex];
        std::vector<const RuntimeAircraftServiceDockBinding*> eligible =
            reloadCandidates(host.hostId);
        if (eligible.empty()) {
            return false;
        }
        if (ammoValue(*findObject(context, eligible.front()->aircraftId)) == 0) {
            eligible.resize(1U);
        }
        for (const RuntimeAircraftServiceDockBinding* binding : eligible) {
            const WorldObjectInstance* aircraft = findObject(
                context,
                binding->aircraftId
            );
            const UnitType* rules = liveAircraftRules(context, aircraft);
            const int before = ammoValue(*aircraft);
            const int after = std::clamp(
                before == std::numeric_limits<int>::max()
                    ? before
                    : before + 1,
                0,
                rules->ammo
            );
            if (after == before) {
                continue;
            }
            ammoLedger[aircraft->instanceId] = after;
            RuntimeAircraftServiceAction action;
            action.kind = RuntimeAircraftServiceActionKind::ReloadAmmo;
            action.hostId = host.hostId;
            action.aircraftId = aircraft->instanceId;
            action.dockIndex = binding->dockIndex;
            action.eventTick = event.eventTick;
            action.owner = aircraft->owner;
            action.ammoBefore = before;
            action.ammoAfter = after;
            action.healthBefore = healthValue(*aircraft);
            action.healthAfter = action.healthBefore;
            action.reason = "docked aircraft reload pulse";
            actions.push_back(std::move(action));
        }
        return true;
    };

    const auto applyRepairPulse = [&](const ScheduledEvent& event) {
        const RuntimeAircraftServiceHostState& host = hosts_[event.hostIndex];
        bool changed = false;
        for (const RuntimeAircraftServiceDockBinding* binding
             : repairCandidates(host.hostId)) {
            const WorldObjectInstance* aircraft = findObject(
                context,
                binding->aircraftId
            );
            const UnitType* rules = liveAircraftRules(context, aircraft);
            const int before = healthValue(*aircraft);
            const std::string ownerKey = economyOwner(aircraft->owner);
            const RepairCharge charge = calculateRepairCharge(
                *rules,
                mechanism,
                before,
                availableCredits(aircraft->owner),
                maximumHealth(*aircraft, *rules)
            );
            if (charge.healedHitPoints <= 0) {
                continue;
            }
            const int after = std::min(
                maximumHealth(*aircraft, *rules),
                before + charge.healedHitPoints
            );
            if (after <= before) {
                continue;
            }
            healthLedger[aircraft->instanceId] = after;
            creditLedger[ownerKey] = std::max(
                0,
                availableCredits(aircraft->owner) - charge.creditCost
            );
            RuntimeAircraftServiceAction action;
            action.kind = RuntimeAircraftServiceActionKind::Repair;
            action.hostId = host.hostId;
            action.aircraftId = aircraft->instanceId;
            action.dockIndex = binding->dockIndex;
            action.eventTick = event.eventTick;
            action.owner = aircraft->owner;
            action.ammoBefore = ammoValue(*aircraft);
            action.ammoAfter = action.ammoBefore;
            action.healthBefore = before;
            action.healthAfter = after;
            action.healedHitPoints = after - before;
            action.creditCost = charge.creditCost;
            action.reason = "docked aircraft repair pulse";
            actions.push_back(std::move(action));
            changed = true;
        }
        return changed;
    };

    std::size_t pulseCount = 0;
    while (!scheduled.empty() && pulseCount < maximumPulsesPerTick) {
        const ScheduledEvent event = scheduled.top();
        scheduled.pop();
        ++pulseCount;
        RuntimeAircraftServiceHostState& host = hosts_[event.hostIndex];
        const bool changed = event.kind
                == RuntimeAircraftServiceActionKind::ReloadAmmo
            ? applyReloadPulse(event)
            : applyRepairPulse(event);
        int& remaining = event.kind
                == RuntimeAircraftServiceActionKind::ReloadAmmo
            ? host.reloadRemainingTicks
            : host.repairRemainingTicks;
        remaining = interval;

        const bool stillEligible = event.kind
                == RuntimeAircraftServiceActionKind::ReloadAmmo
            ? !reloadCandidates(host.hostId).empty()
            : !repairCandidates(host.hostId).empty();
        if (!hostPowered(host.hostId) || !stillEligible) {
            continue;
        }
        if (!changed) {
            const long long tail = static_cast<long long>(currentTick)
                - static_cast<long long>(event.eventTick);
            remaining = interval - static_cast<int>(tail % interval);
            continue;
        }
        const long long nextTick = static_cast<long long>(event.eventTick)
            + static_cast<long long>(interval);
        if (nextTick <= currentTick) {
            remaining = 0;
            scheduled.push({
                static_cast<int>(nextTick),
                event.hostIndex,
                event.kind
            });
        } else {
            remaining = static_cast<int>(nextTick - currentTick);
        }
    }

    lastAdvancedTick_ = currentTick;
    return actions;
}

const std::vector<RuntimeAircraftServiceHostState>&
RuntimeAircraftServiceSystemState::hosts() const {
    return hosts_;
}

const std::vector<RuntimeAircraftServiceDockBinding>&
RuntimeAircraftServiceSystemState::dockBindings() const {
    return dockBindings_;
}

int RuntimeAircraftServiceSystemState::remainingReloadTicks(int hostId) const {
    const auto found = std::lower_bound(
        hosts_.begin(),
        hosts_.end(),
        hostId,
        [](const RuntimeAircraftServiceHostState& host, int id) {
            return host.hostId < id;
        }
    );
    return found != hosts_.end() && found->hostId == hostId
        ? found->reloadRemainingTicks
        : -1;
}

int RuntimeAircraftServiceSystemState::remainingRepairTicks(int hostId) const {
    const auto found = std::lower_bound(
        hosts_.begin(),
        hosts_.end(),
        hostId,
        [](const RuntimeAircraftServiceHostState& host, int id) {
            return host.hostId < id;
        }
    );
    return found != hosts_.end() && found->hostId == hostId
        ? found->repairRemainingTicks
        : -1;
}

int RuntimeAircraftServiceSystemState::lastAdvancedTick() const {
    return lastAdvancedTick_;
}

void RuntimeAircraftServiceSystemState::save(std::ostream& output) const {
    std::vector<RuntimeAircraftServiceHostState> hosts = hosts_;
    std::vector<RuntimeAircraftServiceDockBinding> bindings = dockBindings_;
    std::sort(
        hosts.begin(),
        hosts.end(),
        [](const auto& left, const auto& right) {
            return left.hostId < right.hostId;
        }
    );
    std::sort(
        bindings.begin(),
        bindings.end(),
        [](const auto& left, const auto& right) {
            return bindingKey(left) < bindingKey(right);
        }
    );

    output << "[AircraftServiceRuntime]\n";
    output << "Version=1\n";
    output << "FixedTickHz=" << fixedTickHz_ << "\n";
    output << "LastAdvancedTick=" << lastAdvancedTick_ << "\n";
    output << "HostCount=" << hosts.size() << "\n";
    output << "BindingCount=" << bindings.size() << "\n\n";
    for (std::size_t index = 0; index < hosts.size(); ++index) {
        const RuntimeAircraftServiceHostState& host = hosts[index];
        output << "[AircraftServiceRuntime.Host." << index << "]\n";
        output << "HostId=" << host.hostId << "\n";
        output << "OwnerHex=" << hexEncode(host.owner) << "\n";
        output << "TypeHex=" << hexEncode(host.type) << "\n";
        output << "ReloadRemainingTicks=" << host.reloadRemainingTicks << "\n";
        output << "RepairRemainingTicks=" << host.repairRemainingTicks << "\n\n";
    }
    for (std::size_t index = 0; index < bindings.size(); ++index) {
        const RuntimeAircraftServiceDockBinding& binding = bindings[index];
        output << "[AircraftServiceRuntime.Binding." << index << "]\n";
        output << "HostId=" << binding.hostId << "\n";
        output << "AircraftId=" << binding.aircraftId << "\n";
        output << "DockIndex=" << binding.dockIndex << "\n";
        output << "HostOwnerHex=" << hexEncode(binding.hostOwner) << "\n";
        output << "AircraftOwnerHex=" << hexEncode(binding.aircraftOwner) << "\n";
        output << "HostTypeHex=" << hexEncode(binding.hostType) << "\n";
        output << "AircraftTypeHex=" << hexEncode(binding.aircraftType) << "\n\n";
    }
}

bool RuntimeAircraftServiceSystemState::load(
    const IniFile& ini,
    const RuntimeAircraftServiceContext& context,
    std::string* errorMessage,
    RuntimeAircraftServiceReconcileReport* reconcileReport
) {
    const auto fail = [&](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    if (ini.getSection("AircraftServiceRuntime") == nullptr) {
        RuntimeAircraftServiceSystemState loaded(fixedTickHz_);
        RuntimeAircraftServiceReconcileReport report = loaded.reconcile(context);
        *this = std::move(loaded);
        if (reconcileReport != nullptr) {
            *reconcileReport = std::move(report);
        }
        if (errorMessage != nullptr) {
            errorMessage->clear();
        }
        return true;
    }
    const auto value = [&ini](const std::string& section,
                              const std::string& key) {
        return ini.getValue(section, key, "");
    };

    int version = 0;
    int fixedTickHz = 0;
    int lastAdvancedTick = -1;
    int hostCount = 0;
    int bindingCount = 0;
    if (!parseIntStrict(value("AircraftServiceRuntime", "Version"), version)
        || version != 1
        || !parseIntStrict(
            value("AircraftServiceRuntime", "FixedTickHz"),
            fixedTickHz
        )
        || fixedTickHz < 1
        || fixedTickHz > 1000
        || !parseIntStrict(
            value("AircraftServiceRuntime", "LastAdvancedTick"),
            lastAdvancedTick
        )
        || lastAdvancedTick < -1
        || !parseIntStrict(
            value("AircraftServiceRuntime", "HostCount"),
            hostCount
        )
        || hostCount < 0
        || static_cast<std::size_t>(hostCount) > maximumSavedHosts
        || !parseIntStrict(
            value("AircraftServiceRuntime", "BindingCount"),
            bindingCount
        )
        || bindingCount < 0
        || static_cast<std::size_t>(bindingCount) > maximumSavedBindings) {
        return fail("aircraft service runtime save has invalid header values");
    }

    RuntimeAircraftServiceSystemState loaded(fixedTickHz);
    loaded.lastAdvancedTick_ = lastAdvancedTick;
    std::set<int> hostIds;
    for (int index = 0; index < hostCount; ++index) {
        const std::string section = "AircraftServiceRuntime.Host."
            + std::to_string(index);
        RuntimeAircraftServiceHostState host;
        if (ini.getSection(section) == nullptr
            || !parseIntStrict(value(section, "HostId"), host.hostId)
            || host.hostId < 0
            || !hostIds.insert(host.hostId).second
            || !hexDecode(value(section, "OwnerHex"), host.owner)
            || !hexDecode(value(section, "TypeHex"), host.type)
            || host.type.empty()
            || !parseIntStrict(
                value(section, "ReloadRemainingTicks"),
                host.reloadRemainingTicks
            )
            || host.reloadRemainingTicks < -1
            || !parseIntStrict(
                value(section, "RepairRemainingTicks"),
                host.repairRemainingTicks
            )
            || host.repairRemainingTicks < 0) {
            return fail("aircraft service runtime save has invalid host ["
                + section + "]");
        }
        loaded.hosts_.push_back(std::move(host));
    }

    std::set<std::pair<int, int>> occupiedPads;
    std::set<int> aircraftIds;
    for (int index = 0; index < bindingCount; ++index) {
        const std::string section = "AircraftServiceRuntime.Binding."
            + std::to_string(index);
        RuntimeAircraftServiceDockBinding binding;
        if (ini.getSection(section) == nullptr
            || !parseIntStrict(value(section, "HostId"), binding.hostId)
            || hostIds.find(binding.hostId) == hostIds.end()
            || !parseIntStrict(
                value(section, "AircraftId"),
                binding.aircraftId
            )
            || binding.aircraftId < 0
            || !aircraftIds.insert(binding.aircraftId).second
            || !parseIntStrict(value(section, "DockIndex"), binding.dockIndex)
            || binding.dockIndex < 0
            || binding.dockIndex > maximumDockIndex
            || !occupiedPads.insert({
                binding.hostId,
                binding.dockIndex
            }).second
            || !hexDecode(value(section, "HostOwnerHex"), binding.hostOwner)
            || !hexDecode(
                value(section, "AircraftOwnerHex"),
                binding.aircraftOwner
            )
            || !hexDecode(value(section, "HostTypeHex"), binding.hostType)
            || binding.hostType.empty()
            || !hexDecode(
                value(section, "AircraftTypeHex"),
                binding.aircraftType
            )
            || binding.aircraftType.empty()) {
            return fail("aircraft service runtime save has invalid binding ["
                + section + "]");
        }
        loaded.dockBindings_.push_back(std::move(binding));
    }

    loaded.sortState();
    RuntimeAircraftServiceReconcileReport report = loaded.reconcile(context);
    *this = std::move(loaded);
    if (reconcileReport != nullptr) {
        *reconcileReport = std::move(report);
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

std::uint64_t RuntimeAircraftServiceSystemState::stateHash() const {
    std::ostringstream output;
    save(output);
    return hashText(output.str());
}

void RuntimeAircraftServiceSystemState::sortState() {
    std::sort(
        hosts_.begin(),
        hosts_.end(),
        [](const auto& left, const auto& right) {
            return left.hostId < right.hostId;
        }
    );
    std::sort(
        dockBindings_.begin(),
        dockBindings_.end(),
        [](const auto& left, const auto& right) {
            return bindingKey(left) < bindingKey(right);
        }
    );
}

const char* runtimeAircraftServiceActionName(
    RuntimeAircraftServiceActionKind kind
) {
    switch (kind) {
    case RuntimeAircraftServiceActionKind::ReloadAmmo:
        return "ReloadAmmo";
    case RuntimeAircraftServiceActionKind::Repair:
        return "Repair";
    }
    return "Unknown";
}

}
