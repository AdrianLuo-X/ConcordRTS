#pragma once
#include "simulation/triggers/RuntimeTriggerState.h"
#include <iosfwd>
class IniFile;

// Original first-matching authored trigger lookup, shared with all live callers.
RuntimeTriggerDefinition* runtimeCampaignTriggerById(
    RuntimeTriggerState& triggers,
    const std::string& id
);

// Synchronous original domain lookups and the sole cross-domain catalog write.
// No operation, reference or TeamType pointer escapes its caller's load phase.
class RuntimeTriggerPersistenceOperations {
public:
    virtual ~RuntimeTriggerPersistenceOperations() = default;
    virtual bool objectExists(int objectId) const = 0;
    virtual const RuntimeTeamType* findTeam(const std::string& id) const = 0;
    virtual std::string resolveHouse(const std::string& name) const = 0;
    virtual void applyTechLevel(const std::string& typeKey, int techLevel) = 0;
    virtual bool unitTypeExists(const std::string& id) const = 0;
};

void writeRuntimeCampaignTriggerCollections(const RuntimeTriggerState& triggers, std::ostream& output);
void writeRuntimeTeamMissionState(const RuntimeTriggerState& triggers, std::ostream& output);
void restoreRuntimeCampaignTriggerCollections(RuntimeTriggerState& triggers, const IniFile& ini,
    const int& tick, const std::set<std::string>& validatedAlliedPairs,
    RuntimeTriggerPersistenceOperations& operations);
void restoreRuntimeTeamMissionState(RuntimeTriggerState& triggers, const IniFile& ini,
    int version, const bool& campaignMode, RuntimeTriggerPersistenceOperations& operations);
void writeRuntimeTriggerReinforcements(const RuntimeTriggerState& triggers, std::ostream& output);
bool readRuntimeTriggerReinforcements(RuntimeTriggerState& triggers, const IniFile& ini,
    std::string& error, RuntimeTriggerPersistenceOperations& operations);
