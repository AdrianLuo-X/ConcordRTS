#include "RuntimeHistoryOwnerCensusContract.h"
#include <iostream>
int main() {
    std::string error;
    if (!runRuntimeHistoryOwnerCensusContract(error)) { std::cerr << error << '\n'; return 1; }
    std::cout << "History owner census passed: 12 orders, 10000 historical objects, "
        "canonical collisions, priority, embedded NUL, non-ASCII, dead-house retention.\n";
    return 0;
}
