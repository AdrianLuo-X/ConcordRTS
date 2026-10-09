#pragma once

#include "GameWorld.h"
#include "RulesDatabase.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

class IniFile;

// The aircraft service core consumes the world's existing dock mirror. It
// never owns or mutates a second host/pad relation.
struct RuntimeAircraftServiceContext {
    const RulesDatabase* rules = nullptr;
    const std::vector<WorldObjectInstance>* objects = nullptr;

    std::function<bool(const std::string&, const std::string&)> areAllied;
    std::function<int(const std::string&)> creditBalance;
    std::function<std::string(const std::string&)> canonicalOwnerId;
    // Difficulty, country and veterancy may change a live aircraft's strength.
    std::function<int(const WorldObjectInstance&)> maximumHealth;
};

enum class RuntimeAircraftServiceActionKind {
    ReloadAmmo,
    Repair
};

// Actions are value-only. The host applies ammo, health and credit mutations
// after leaving object iteration.
struct RuntimeAircraftServiceAction {
    RuntimeAircraftServiceActionKind kind =
        RuntimeAircraftServiceActionKind::ReloadAmmo;
    int hostId = -1;
    int aircraftId = -1;
    int dockIndex = -1;
    int eventTick = -1;
    std::string owner;
    int ammoBefore = -1;
    int ammoAfter = -1;
    int healthBefore = -1;
    int healthAfter = -1;
    int healedHitPoints = 0;
    int creditCost = 0;
    std::string reason;
};

struct RuntimeAircraftServiceHostState {
    int hostId = -1;
    std::string owner;
    std::string type;
    // -1 is an unarmed reload timer. Repair starts at zero so the first
    // damaged aircraft receives an immediate service pulse.
    int reloadRemainingTicks = -1;
    int repairRemainingTicks = 0;
};

struct RuntimeAircraftServiceDockBinding {
    int hostId = -1;
    int aircraftId = -1;
    int dockIndex = -1;
    std::string hostOwner;
    std::string aircraftOwner;
    std::string hostType;
    std::string aircraftType;
};

struct RuntimeAircraftServiceReconcileReport {
    bool contextValid = false;
    std::size_t retainedHosts = 0;
    std::size_t addedHosts = 0;
    std::size_t removedHosts = 0;
    std::size_t resetHosts = 0;
    std::size_t retainedBindings = 0;
    std::size_t addedBindings = 0;
    std::size_t removedBindings = 0;
    std::size_t duplicateDockBindings = 0;
    std::size_t duplicateAircraftBindings = 0;
    std::size_t skippedHostsByLimit = 0;
    std::size_t skippedBindingsByLimit = 0;
};

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

const char* runtimeAircraftServiceActionName(
    RuntimeAircraftServiceActionKind kind
);
