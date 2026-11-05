#pragma once
#include "simulation/ai/RuntimeComputerTactics.h"
#include <iosfwd>
class IniFile;

// Original Schema 2 records in map order; no host/world/window dependency.
void writeRuntimeComputerTacticsState(std::ostream& output,
    const std::map<std::string, RuntimeComputerTacticsState>& tacticsByOwner);

// Accept original Schema 1/2. Missing section clears on success;
// any rejected record leaves the caller's map unchanged.
bool readRuntimeComputerTactics(const IniFile& ini,
    std::map<std::string, RuntimeComputerTacticsState>& output, std::string& error);
