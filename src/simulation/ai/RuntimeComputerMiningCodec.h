#pragma once

#include "simulation/ai/RuntimeComputerMining.h"

#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>

class IniFile;

// Schema 1: ordered owners and six counters; the caller owns format orchestration.
void writeRuntimeComputerMining(
    std::ostream& output,
    const std::map<std::string, RuntimeComputerMiningState>& miningByOwner
);

// Missing sections clear output on success. Failed reads leave output unchanged.
bool readRuntimeComputerMining(
    const IniFile& ini,
    std::map<std::string, RuntimeComputerMiningState>& output,
    std::string& error
);

// Append the original contribution to an existing host hash. Empty maps add nothing.
void mixRuntimeComputerMiningHash(
    std::uint64_t& hash,
    const std::map<std::string, RuntimeComputerMiningState>& miningByOwner
);
