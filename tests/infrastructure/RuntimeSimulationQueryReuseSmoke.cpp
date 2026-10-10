#include "runtime/performance/diagnostics/RuntimeSimulationQueryReusePrimitiveContract.inl"

int main() {
    std::string error;
    if (!runRuntimeSimulationQueryReusePrimitiveContract(error)) {
        std::cerr << error << '\n';
        return 1;
    }
    return 0;
}
