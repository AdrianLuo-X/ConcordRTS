#pragma once

// Credits remain in the existing AI/campaign house account. This record
// retains the ore-storage and delivery history used by the shared miners.
struct RuntimeComputerMiningState {
    int storedResourceUnits = 0;
    int storedResourceValue = 0;
    int storedResourceGemUnits = 0;
    int resourceTrips = 0;
    int harvesterTeleports = 0;
    int slaveMinerLoads = 0;
};
