#include "RuntimeWaterWakeHistoryMaintenanceContract.inl"

int main() {
    std::string error;
    if (!runRuntimeWaterWakeHistoryMaintenanceContract(error)) {
        std::cerr << error << '\n';
        return 1;
    }
    return 0;
}
